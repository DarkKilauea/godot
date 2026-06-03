/**************************************************************************/
/*  vulkan_hdr_swap_chain.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#ifdef VULKAN_ENABLED

// Enable weak-symbol availability guards so we can call ASurfaceControl /
// ASurfaceTransaction APIs (introduced after our min SDK) inside
// `if (__builtin_available(android N, *))` blocks. Must precede any NDK
// header include.
#ifndef __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__
#define __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__
#endif

#include "core/error/error_list.h"
#include "core/os/mutex.h"
#include "core/templates/local_vector.h"
#include "core/templates/safe_refcount.h"

#include <android/data_space.h>
#include <android/hardware_buffer.h>
#include <android/surface_control.h>
#include <drivers/vulkan/godot_vulkan.h>

#include <condition_variable>
#include <mutex>

// Replaces VkSwapchainKHR on Android when HDR output is requested. Vulkan's
// swapchain extension on Android cannot communicate the extended-range buffer
// ratio (currentBufferRatio) that the compositor needs in order to actually
// display values above SDR white, so we present through ASurfaceControl /
// ASurfaceTransaction directly with AHardwareBuffer-backed VkImages.
//
// Requires Android API 34 (UPSIDE_DOWN_CAKE) for
// ASurfaceTransaction_setExtendedRangeBrightness. The caller is responsible
// for the API gate.
class VulkanHDRSwapChain {
public:
	struct CreateParams {
		VkDevice device = VK_NULL_HANDLE;
		VkPhysicalDevice physical_device = VK_NULL_HANDLE;
		ANativeWindow *parent_window = nullptr;
		VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
		VkExtent2D extent = {};
		uint32_t buffer_count = 3;
		VkImageUsageFlags image_usage = 0;
		ADataSpace dataspace = ADATASPACE_SCRGB_LINEAR;
		const char *debug_name = "Godot HDR";
	};

	VulkanHDRSwapChain() = default;
	~VulkanHDRSwapChain();

	VulkanHDRSwapChain(const VulkanHDRSwapChain &) = delete;
	VulkanHDRSwapChain &operator=(const VulkanHDRSwapChain &) = delete;

	// One-time setup. Returns OK on success. On failure, the object remains
	// in a destroyed state and can safely be deleted.
	Error create(const CreateParams &p_params);

	// Tears down the compositor layer and releases GPU resources without
	// blocking on outstanding compositor callbacks, then drops the owner's
	// reference. The pointer is invalid after this call. If pending OnComplete
	// callbacks are still in flight, the underlying object lives just long
	// enough for them to fire harmlessly.
	static void release(VulkanHDRSwapChain *p_chain);

	// Picks the next image to render into and arranges for `p_signal_semaphore`
	// to be signaled once the buffer is safe to write (i.e., once the previous
	// presentation of this buffer has been released by the compositor).
	Error acquire_next_image(VkSemaphore p_signal_semaphore, uint32_t *r_image_index);

	// Submits the image to the compositor with the given HDR metadata. Waits
	// for `p_wait_semaphore` to be signaled before sampling the buffer.
	// `p_current_buffer_ratio` tells the compositor what HDR/SDR ratio the
	// buffer's [0, max] linear range represents; `p_desired_ratio` is the
	// peak we'd like the panel to deliver. Both must be >= 1.0f.
	Error present(uint32_t p_image_index, VkSemaphore p_wait_semaphore,
			float p_current_buffer_ratio, float p_desired_ratio);

	bool is_valid() const { return surface_control != nullptr; }
	uint32_t get_image_count() const { return buffers.size(); }
	VkImage get_image(uint32_t p_index) const;
	VkImageView get_image_view(uint32_t p_index) const;
	VkFormat get_format() const { return format; }
	VkExtent2D get_extent() const { return extent; }

	// True when the running device supports the AHB HDR path. Reflects only
	// the OS-level prerequisites (API >= 34); the actual decision to use the
	// path is still made at swap-chain creation time and additionally depends
	// on the presence of the required Vulkan device extensions. Code outside
	// the Vulkan driver can use this to avoid calling APIs (such as
	// `SurfaceView.setDesiredHdrHeadroom`) that would interfere with the
	// AHB child SurfaceControl whenever the path is likely to be selected.
	static bool is_supported();

	// True when any VulkanHDRSwapChain instance is currently presenting
	// frames via ASurfaceControl. Used by the rest of the Android display
	// stack to suppress calls that would touch the parent SurfaceView's
	// SurfaceControl (e.g. SurfaceView.setDesiredHdrHeadroom) since those
	// can tear down and re-create the parent SC, orphaning our child SC's
	// in-flight transactions and stalling the buffer queue.
	static bool is_any_presenter_active();

private:
	enum class BufferState : uint8_t {
		FREE, // Not in flight; safe to acquire.
		ACQUIRED, // Handed to the renderer; awaiting present.
		IN_FLIGHT, // Submitted to the compositor; awaiting release fence.
		RELEASE_PENDING // Release fence FD received; pending re-acquire.
	};

	struct Buffer {
		AHardwareBuffer *ahb = nullptr;
		VkImage image = VK_NULL_HANDLE;
		VkImageView image_view = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		// Most recent release fence FD from the compositor; -1 means "not yet
		// presented" or "already consumed". Owned by this struct until imported
		// into a VkSemaphore in `acquire_next_image`.
		int release_fence_fd = -1;
		BufferState state = BufferState::FREE;
	};

	struct PendingPresent {
		VulkanHDRSwapChain *chain = nullptr;
		uint32_t replaced_index = UINT32_MAX;
	};

	Error _allocate_buffer(Buffer &r_buffer);
	void _free_buffer(Buffer &r_buffer);
	bool _load_function_pointers();
	void _consume_release_fence_locked(uint32_t p_index, int p_fd);

	// Internal teardown. Tears down the compositor layer and frees GPU/AHB
	// resources but does NOT delete the C++ object itself; that happens via
	// `release` once outstanding OnComplete callbacks are done with us.
	void _destroy();

	static void _release_lifetime_ref(VulkanHDRSwapChain *p_chain);

	static void _on_transaction_complete(void *p_context,
			ASurfaceTransactionStats *p_stats);

	// Process-wide count of HDR AHB chains that have successfully created
	// their compositor layer. Used by `is_any_presenter_active`.
	static SafeNumeric<int> active_presenter_count;

	// Vulkan state.
	VkDevice device = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;

	PFN_vkGetAndroidHardwareBufferPropertiesANDROID _vkGetAndroidHardwareBufferPropertiesANDROID = nullptr;
	PFN_vkImportSemaphoreFdKHR _vkImportSemaphoreFdKHR = nullptr;
	PFN_vkGetSemaphoreFdKHR _vkGetSemaphoreFdKHR = nullptr;

	// Native compositor state.
	ASurfaceControl *surface_control = nullptr;

	// Buffer pool. `acquire_mutex` (std::mutex) guards every `Buffer::state`
	// and `Buffer::release_fence_fd` mutation, the `destroyed` flag, and
	// `last_presented_index`, since OnComplete callbacks fire on a binder
	// thread. We use std::mutex (rather than Godot's BinaryMutex) so we can
	// pair it with a std::condition_variable that supports a bounded
	// `wait_for`, which `acquire_next_image` needs to ride out the ~1 vsync
	// compositor latency at startup without triggering a swap-chain resize.
	mutable std::mutex acquire_mutex;
	mutable std::condition_variable buffer_available_cv;
	LocalVector<Buffer> buffers;
	uint32_t next_acquire_index = 0;
	uint32_t last_presented_index = UINT32_MAX;
	bool destroyed = false;
	// Whether this instance currently contributes to active_presenter_count.
	// Set on successful create, cleared on _destroy. Prevents double-decrement
	// if _destroy is called more than once (which is normal because both
	// `release` and the destructor invoke it).
	bool presenter_counted = false;

	// Refcount shared between the owner (the wrapping SwapChain) and every
	// in-flight present transaction's OnComplete callback. Starts at 1 for
	// the owner; `release` drops that ref. Each `present` increments the
	// count before applying, and the OnComplete decrements when it fires.
	// When the last ref is dropped the object self-deletes.
	SafeNumeric<int> lifetime_refs{ 1 };

	// Static configuration captured at create-time.
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkExtent2D extent = {};
	VkImageUsageFlags image_usage = 0;
	ADataSpace dataspace = ADATASPACE_UNKNOWN;
};

#endif // VULKAN_ENABLED
