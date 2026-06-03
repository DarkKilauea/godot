/**************************************************************************/
/*  vulkan_hdr_swap_chain.cpp                                             */
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

#ifdef VULKAN_ENABLED

#include "vulkan_hdr_swap_chain.h"

#include "core/error/error_macros.h"
#include "core/variant/variant.h"

#include <android/api-level.h>
#include <unistd.h>

SafeNumeric<int> VulkanHDRSwapChain::active_presenter_count{ 0 };

bool VulkanHDRSwapChain::is_supported() {
	return android_get_device_api_level() >= 34;
}

bool VulkanHDRSwapChain::is_any_presenter_active() {
	return active_presenter_count.get() > 0;
}

VulkanHDRSwapChain::~VulkanHDRSwapChain() {
	_destroy();
}

bool VulkanHDRSwapChain::_load_function_pointers() {
	_vkGetAndroidHardwareBufferPropertiesANDROID =
			(PFN_vkGetAndroidHardwareBufferPropertiesANDROID)
					vkGetDeviceProcAddr(device, "vkGetAndroidHardwareBufferPropertiesANDROID");
	_vkImportSemaphoreFdKHR =
			(PFN_vkImportSemaphoreFdKHR)
					vkGetDeviceProcAddr(device, "vkImportSemaphoreFdKHR");
	_vkGetSemaphoreFdKHR =
			(PFN_vkGetSemaphoreFdKHR)
					vkGetDeviceProcAddr(device, "vkGetSemaphoreFdKHR");
	return _vkGetAndroidHardwareBufferPropertiesANDROID &&
			_vkImportSemaphoreFdKHR && _vkGetSemaphoreFdKHR;
}

Error VulkanHDRSwapChain::create(const CreateParams &p_params) {
	ERR_FAIL_COND_V(surface_control != nullptr, ERR_ALREADY_EXISTS);
	ERR_FAIL_NULL_V(p_params.device, ERR_INVALID_PARAMETER);
	ERR_FAIL_NULL_V(p_params.physical_device, ERR_INVALID_PARAMETER);
	ERR_FAIL_NULL_V(p_params.parent_window, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_params.buffer_count == 0, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_params.extent.width == 0 || p_params.extent.height == 0, ERR_INVALID_PARAMETER);

	// The caller is expected to API-gate on 34+, but we double-check here so
	// the NDK availability annotations are satisfied at compile time and so
	// nothing crashes if the gate is ever bypassed.
	if (!__builtin_available(android 34, *)) {
		return ERR_UNAVAILABLE;
	}

	device = p_params.device;
	physical_device = p_params.physical_device;
	format = p_params.format;
	extent = p_params.extent;
	image_usage = p_params.image_usage;
	dataspace = p_params.dataspace;

	if (!_load_function_pointers()) {
		ERR_PRINT("VulkanHDRSwapChain: required Vulkan device extensions are missing "
				  "(VK_ANDROID_external_memory_android_hardware_buffer / "
				  "VK_KHR_external_semaphore_fd).");
		_destroy();
		return ERR_UNAVAILABLE;
	}

	if (__builtin_available(android 34, *)) {
		surface_control = ASurfaceControl_createFromWindow(
				p_params.parent_window, p_params.debug_name);
	}
	if (surface_control == nullptr) {
		ERR_PRINT("VulkanHDRSwapChain: ASurfaceControl_createFromWindow failed.");
		_destroy();
		return ERR_CANT_CREATE;
	}

	buffers.resize(p_params.buffer_count);
	for (uint32_t i = 0; i < buffers.size(); i++) {
		Error err = _allocate_buffer(buffers[i]);
		if (err != OK) {
			ERR_PRINT(vformat("VulkanHDRSwapChain: failed to allocate buffer %d.", i));
			_destroy();
			return err;
		}
	}

	// All resources are live; advertise that the AHB presenter owns headroom
	// for this process. Paired with the decrement in `_destroy`.
	active_presenter_count.increment();
	presenter_counted = true;

	return OK;
}

Error VulkanHDRSwapChain::_allocate_buffer(Buffer &r_buffer) {
	AHardwareBuffer_Desc ahb_desc = {};
	ahb_desc.width = extent.width;
	ahb_desc.height = extent.height;
	ahb_desc.layers = 1;
	// We only support the fp16 path. Other formats can be added when needed.
	ERR_FAIL_COND_V_MSG(format != VK_FORMAT_R16G16B16A16_SFLOAT, ERR_INVALID_PARAMETER,
			"VulkanHDRSwapChain: only VK_FORMAT_R16G16B16A16_SFLOAT is currently supported.");
	ahb_desc.format = AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT;
	ahb_desc.usage = AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
			AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
			AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY;

	int ahb_err = -1;
	if (__builtin_available(android 34, *)) {
		ahb_err = AHardwareBuffer_allocate(&ahb_desc, &r_buffer.ahb);
	}
	ERR_FAIL_COND_V_MSG(ahb_err != 0 || r_buffer.ahb == nullptr, ERR_CANT_CREATE,
			vformat("AHardwareBuffer_allocate failed (%d).", ahb_err));

	VkExternalMemoryImageCreateInfo external_image_info = {};
	external_image_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
	external_image_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

	VkImageCreateInfo image_info = {};
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.pNext = &external_image_info;
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = format;
	image_info.extent = { extent.width, extent.height, 1 };
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = image_usage;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkResult vk_err = vkCreateImage(device, &image_info, nullptr, &r_buffer.image);
	ERR_FAIL_COND_V_MSG(vk_err != VK_SUCCESS, ERR_CANT_CREATE,
			vformat("vkCreateImage failed (%d).", vk_err));

	VkAndroidHardwareBufferPropertiesANDROID ahb_props = {};
	ahb_props.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
	vk_err = _vkGetAndroidHardwareBufferPropertiesANDROID(device, r_buffer.ahb, &ahb_props);
	ERR_FAIL_COND_V_MSG(vk_err != VK_SUCCESS, ERR_CANT_CREATE,
			vformat("vkGetAndroidHardwareBufferPropertiesANDROID failed (%d).", vk_err));

	VkPhysicalDeviceMemoryProperties mem_props = {};
	vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);
	uint32_t memory_type_index = UINT32_MAX;
	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
		if ((ahb_props.memoryTypeBits & (1u << i)) != 0 &&
				(mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
			memory_type_index = i;
			break;
		}
	}
	if (memory_type_index == UINT32_MAX) {
		// Fall back to any allowed type if no device-local match found.
		for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
			if ((ahb_props.memoryTypeBits & (1u << i)) != 0) {
				memory_type_index = i;
				break;
			}
		}
	}
	ERR_FAIL_COND_V_MSG(memory_type_index == UINT32_MAX, ERR_CANT_CREATE,
			"VulkanHDRSwapChain: no compatible memory type for AHardwareBuffer import.");

	VkMemoryDedicatedAllocateInfo dedicated_info = {};
	dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
	dedicated_info.image = r_buffer.image;

	VkImportAndroidHardwareBufferInfoANDROID import_info = {};
	import_info.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
	import_info.pNext = &dedicated_info;
	import_info.buffer = r_buffer.ahb;

	VkMemoryAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = &import_info;
	alloc_info.allocationSize = ahb_props.allocationSize;
	alloc_info.memoryTypeIndex = memory_type_index;

	vk_err = vkAllocateMemory(device, &alloc_info, nullptr, &r_buffer.memory);
	ERR_FAIL_COND_V_MSG(vk_err != VK_SUCCESS, ERR_CANT_CREATE,
			vformat("vkAllocateMemory (AHB import) failed (%d).", vk_err));

	vk_err = vkBindImageMemory(device, r_buffer.image, r_buffer.memory, 0);
	ERR_FAIL_COND_V_MSG(vk_err != VK_SUCCESS, ERR_CANT_CREATE,
			vformat("vkBindImageMemory failed (%d).", vk_err));

	VkImageViewCreateInfo view_info = {};
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = r_buffer.image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = format;
	view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.baseMipLevel = 0;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = 1;

	vk_err = vkCreateImageView(device, &view_info, nullptr, &r_buffer.image_view);
	ERR_FAIL_COND_V_MSG(vk_err != VK_SUCCESS, ERR_CANT_CREATE,
			vformat("vkCreateImageView failed (%d).", vk_err));

	r_buffer.state = BufferState::FREE;
	r_buffer.release_fence_fd = -1;
	return OK;
}

void VulkanHDRSwapChain::_free_buffer(Buffer &r_buffer) {
	if (r_buffer.release_fence_fd >= 0) {
		close(r_buffer.release_fence_fd);
		r_buffer.release_fence_fd = -1;
	}
	if (r_buffer.image_view != VK_NULL_HANDLE) {
		vkDestroyImageView(device, r_buffer.image_view, nullptr);
		r_buffer.image_view = VK_NULL_HANDLE;
	}
	if (r_buffer.image != VK_NULL_HANDLE) {
		vkDestroyImage(device, r_buffer.image, nullptr);
		r_buffer.image = VK_NULL_HANDLE;
	}
	if (r_buffer.memory != VK_NULL_HANDLE) {
		vkFreeMemory(device, r_buffer.memory, nullptr);
		r_buffer.memory = VK_NULL_HANDLE;
	}
	if (r_buffer.ahb != nullptr) {
		if (__builtin_available(android 34, *)) {
			AHardwareBuffer_release(r_buffer.ahb);
		}
		r_buffer.ahb = nullptr;
	}
	r_buffer.state = BufferState::FREE;
}

void VulkanHDRSwapChain::_destroy() {
	{
		std::unique_lock<std::mutex> lock(acquire_mutex);
		if (destroyed) {
			return;
		}
		destroyed = true;
	}
	// Wake any thread blocked in `acquire_next_image` so it can bail out.
	buffer_available_cv.notify_all();

	// Wait for GPU work to drain before we free VkImages/AHardwareBuffers. We
	// don't wait for compositor OnComplete callbacks here -- they keep the
	// object alive via `lifetime_refs` and will see `destroyed` set so they
	// won't touch the freed buffers.
	if (device != VK_NULL_HANDLE) {
		vkDeviceWaitIdle(device);
	}

	{
		std::unique_lock<std::mutex> lock(acquire_mutex);
		for (Buffer &b : buffers) {
			_free_buffer(b);
		}
		buffers.clear();
	}

	if (surface_control != nullptr) {
		if (__builtin_available(android 34, *)) {
			// Detach the layer from its parent before releasing the handle,
			// otherwise the compositor will keep displaying our last frame
			// on top of whatever the application switches to (e.g. a plain
			// VkSwapchain when HDR is toggled off).
			ASurfaceTransaction *txn = ASurfaceTransaction_create();
			if (txn != nullptr) {
				ASurfaceTransaction_setVisibility(txn, surface_control, ASURFACE_TRANSACTION_VISIBILITY_HIDE);
				ASurfaceTransaction_reparent(txn, surface_control, nullptr);
				ASurfaceTransaction_apply(txn);
				ASurfaceTransaction_delete(txn);
			}
			ASurfaceControl_release(surface_control);
		}
		surface_control = nullptr;
	}

	next_acquire_index = 0;
	last_presented_index = UINT32_MAX;
	device = VK_NULL_HANDLE;
	physical_device = VK_NULL_HANDLE;
	_vkGetAndroidHardwareBufferPropertiesANDROID = nullptr;
	_vkImportSemaphoreFdKHR = nullptr;
	_vkGetSemaphoreFdKHR = nullptr;

	if (presenter_counted) {
		active_presenter_count.decrement();
		presenter_counted = false;
	}
}

void VulkanHDRSwapChain::release(VulkanHDRSwapChain *p_chain) {
	if (p_chain == nullptr) {
		return;
	}
	p_chain->_destroy();
	_release_lifetime_ref(p_chain);
}

void VulkanHDRSwapChain::_release_lifetime_ref(VulkanHDRSwapChain *p_chain) {
	if (p_chain == nullptr) {
		return;
	}
	if (p_chain->lifetime_refs.decrement() == 0) {
		memdelete(p_chain);
	}
}

VkImage VulkanHDRSwapChain::get_image(uint32_t p_index) const {
	ERR_FAIL_UNSIGNED_INDEX_V(p_index, buffers.size(), VK_NULL_HANDLE);
	return buffers[p_index].image;
}

VkImageView VulkanHDRSwapChain::get_image_view(uint32_t p_index) const {
	ERR_FAIL_UNSIGNED_INDEX_V(p_index, buffers.size(), VK_NULL_HANDLE);
	return buffers[p_index].image_view;
}

Error VulkanHDRSwapChain::acquire_next_image(VkSemaphore p_signal_semaphore, uint32_t *r_image_index) {
	ERR_FAIL_NULL_V(r_image_index, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(!is_valid(), ERR_UNCONFIGURED);
	ERR_FAIL_COND_V(p_signal_semaphore == VK_NULL_HANDLE, ERR_INVALID_PARAMETER);

	int release_fd = -1;
	uint32_t index = UINT32_MAX;
	{
		std::unique_lock<std::mutex> lock(acquire_mutex);

		const uint32_t count = buffers.size();

		// Block until either a buffer becomes usable or the chain is being
		// torn down. We deliberately do not impose a refresh-rate-dependent
		// timeout here: compositor release latency on Android varies with
		// the display vsync period (~8 ms at 120 Hz, ~33 ms at 30 Hz), and
		// any guess we make would either under-wait on slow panels (causing
		// spurious swap-chain resize loops) or over-wait on fast ones
		// (adding unnecessary latency). The condition variable is woken
		// from `_on_transaction_complete` whenever a buffer is released and
		// from `_destroy` on shutdown, so the only way to hang here is a
		// compositor that has stopped responding entirely, in which case
		// the render thread should block until the system either recovers
		// or tears us down.
		const auto find_free = [&]() {
			const uint32_t start = next_acquire_index;
			for (uint32_t attempt = 0; attempt < count; attempt++) {
				uint32_t candidate = (start + attempt) % count;
				BufferState state = buffers[candidate].state;
				if (state == BufferState::FREE || state == BufferState::RELEASE_PENDING) {
					return candidate;
				}
			}
			return UINT32_MAX;
		};

		buffer_available_cv.wait(lock, [&]() {
			if (destroyed) {
				return true;
			}
			index = find_free();
			return index != UINT32_MAX;
		});

		if (destroyed) {
			return ERR_UNAVAILABLE;
		}

		release_fd = buffers[index].release_fence_fd;
		buffers[index].release_fence_fd = -1;
		buffers[index].state = BufferState::ACQUIRED;
		next_acquire_index = (index + 1) % count;
	}

	// Import the release fence (or -1 for "already signaled") into the caller's
	// semaphore. The semaphore takes ownership of the FD on success.
	VkImportSemaphoreFdInfoKHR import_info = {};
	import_info.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR;
	import_info.semaphore = p_signal_semaphore;
	import_info.flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT;
	import_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
	import_info.fd = release_fd;

	VkResult vk_err = _vkImportSemaphoreFdKHR(device, &import_info);
	if (vk_err != VK_SUCCESS) {
		if (release_fd >= 0) {
			close(release_fd);
		}
		ERR_FAIL_V_MSG(ERR_CANT_CREATE,
				vformat("vkImportSemaphoreFdKHR failed (%d).", vk_err));
	}

	*r_image_index = index;
	return OK;
}

Error VulkanHDRSwapChain::present(uint32_t p_image_index, VkSemaphore p_wait_semaphore,
		float p_current_buffer_ratio, float p_desired_ratio) {
	ERR_FAIL_COND_V(!is_valid(), ERR_UNCONFIGURED);
	ERR_FAIL_UNSIGNED_INDEX_V(p_image_index, buffers.size(), ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_wait_semaphore == VK_NULL_HANDLE, ERR_INVALID_PARAMETER);

	// Export the wait semaphore as a sync_fd that the compositor will block on
	// before sampling the buffer. The export consumes the semaphore's payload.
	VkSemaphoreGetFdInfoKHR get_fd_info = {};
	get_fd_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
	get_fd_info.semaphore = p_wait_semaphore;
	get_fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;

	int acquire_fd = -1;
	VkResult vk_err = _vkGetSemaphoreFdKHR(device, &get_fd_info, &acquire_fd);
	if (vk_err != VK_SUCCESS) {
		ERR_FAIL_V_MSG(ERR_CANT_CREATE,
				vformat("vkGetSemaphoreFdKHR failed (%d).", vk_err));
	}

	if (!__builtin_available(android 34, *)) {
		if (acquire_fd >= 0) {
			close(acquire_fd);
		}
		return ERR_UNAVAILABLE;
	}

	ASurfaceTransaction *txn = ASurfaceTransaction_create();
	if (txn == nullptr) {
		if (acquire_fd >= 0) {
			close(acquire_fd);
		}
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, "ASurfaceTransaction_create failed.");
	}

	// ASurfaceTransaction_setBuffer takes ownership of the acquire FD on success.
	ASurfaceTransaction_setBuffer(txn, surface_control, buffers[p_image_index].ahb, acquire_fd);
	ASurfaceTransaction_setBufferDataSpace(txn, surface_control, dataspace);
	ASurfaceTransaction_setVisibility(txn, surface_control, ASURFACE_TRANSACTION_VISIBILITY_SHOW);
	ASurfaceTransaction_setExtendedRangeBrightness(txn, surface_control,
			p_current_buffer_ratio, p_desired_ratio);
	// Also hint the panel-level HDR headroom directly on our child layer when
	// the API is available (Android 15 / API 35). The SurfaceView's
	// setDesiredHdrHeadroom applies to a different SC, so without this the
	// compositor may keep the display in SDR even though our layer carries
	// extended-range content.
	if (__builtin_available(android 35, *)) {
		ASurfaceTransaction_setDesiredHdrHeadroom(txn, surface_control, p_desired_ratio);
	}

	static int s_present_log = 0;
	if (s_present_log < 10 || (s_present_log % 120) == 0) {
		print_line(vformat("HDR AHB present #%d: idx=%d buf=%dx%d dataspace=%d current=%.2f desired=%.2f",
				s_present_log, int(p_image_index),
				int(extent.width), int(extent.height), int(dataspace),
				double(p_current_buffer_ratio), double(p_desired_ratio)));
	}
	s_present_log++;

	// Mark in flight and record which buffer this transaction will replace so
	// the OnComplete callback can attribute the release fence correctly.
	uint32_t replaced_index;
	{
		std::unique_lock<std::mutex> lock(acquire_mutex);
		buffers[p_image_index].state = BufferState::IN_FLIGHT;
		replaced_index = last_presented_index;
		last_presented_index = p_image_index;
	}

	PendingPresent *pending = memnew(PendingPresent);
	pending->chain = this;
	pending->replaced_index = replaced_index;

	// Keep this object alive at least until the compositor's OnComplete
	// fires. The owner's `release` may run any time after we apply the
	// transaction; without this extra ref the callback could land on freed
	// memory.
	lifetime_refs.increment();

	ASurfaceTransaction_setOnComplete(txn, pending, &VulkanHDRSwapChain::_on_transaction_complete);
	ASurfaceTransaction_apply(txn);
	ASurfaceTransaction_delete(txn);

	return OK;
}

void VulkanHDRSwapChain::_consume_release_fence_locked(uint32_t p_index, int p_fd) {
	// Caller holds acquire_mutex.
	if (p_index >= buffers.size()) {
		if (p_fd >= 0) {
			close(p_fd);
		}
		return;
	}

	// If somehow we already have a stashed fence (shouldn't happen, but defend
	// against double-callbacks), close the old one rather than leak the FD.
	if (buffers[p_index].release_fence_fd >= 0) {
		close(buffers[p_index].release_fence_fd);
	}
	buffers[p_index].release_fence_fd = p_fd;
	buffers[p_index].state = BufferState::RELEASE_PENDING;
}

void VulkanHDRSwapChain::_on_transaction_complete(void *p_context,
		ASurfaceTransactionStats *p_stats) {
	PendingPresent *pending = static_cast<PendingPresent *>(p_context);
	if (pending == nullptr) {
		return;
	}
	VulkanHDRSwapChain *chain = pending->chain;
	uint32_t replaced_index = pending->replaced_index;
	memdelete(pending);

	if (chain == nullptr) {
		return;
	}

	int release_fd = -1;
	if (__builtin_available(android 34, *)) {
		if (chain->surface_control != nullptr && replaced_index != UINT32_MAX) {
			release_fd = ASurfaceTransactionStats_getPreviousReleaseFenceFd(p_stats, chain->surface_control);
		}
	}

	static int s_complete_log = 0;
	if (s_complete_log < 10 || (s_complete_log % 120) == 0) {
		print_line(vformat("HDR AHB on_complete #%d: replaced_index=%d release_fd=%d destroyed=%d",
				s_complete_log, (int)replaced_index, release_fd, (int)chain->destroyed));
	}
	s_complete_log++;

	bool buffer_became_available = false;
	{
		std::unique_lock<std::mutex> lock(chain->acquire_mutex);
		if (chain->destroyed || replaced_index == UINT32_MAX) {
			// The chain has been torn down (or this is the first present and
			// there is no previous buffer to release). Either way, we must
			// not touch `buffers`; just drop the fd.
			if (release_fd >= 0) {
				close(release_fd);
			}
		} else {
			chain->_consume_release_fence_locked(replaced_index, release_fd);
			buffer_became_available = true;
		}
	}

	if (buffer_became_available) {
		// Wake any thread blocked in `acquire_next_image` waiting for a
		// buffer to become FREE/RELEASE_PENDING.
		chain->buffer_available_cv.notify_one();
	}

	_release_lifetime_ref(chain);
}

#endif // VULKAN_ENABLED
