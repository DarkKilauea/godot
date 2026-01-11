/**************************************************************************/
/*  hdr_options_popup.h                                                   */
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

#include "scene/gui/box_container.h"
#include "scene/gui/check_box.h"
#include "scene/gui/label.h"
#include "scene/gui/popup.h"
#include "scene/gui/slider.h"
#include "scene/gui/spin_box.h"

class HDROptionsPopup : public PopupPanel {
	GDCLASS(HDROptionsPopup, PopupPanel);

public:
	struct HDRSettings {
		bool requested = false;
		bool enabled = false;
		bool auto_ref_luminance = true;
		bool auto_max_luminance = true;
		float reference_luminance = 100.0;
		float max_luminance = 1000.0;
		float current_ref_luminance = 100.0;
		float current_max_luminance = 1000.0;
		float max_color_value = 1.0;
		int error_code = 0;  // 0 = no error, 1 = not supported, 2 = not available
	};

private:
	Label *max_color_label = nullptr;
	CheckBox *request_checkbox = nullptr;
	Label *error_label = nullptr;
	HBoxContainer *luminance_container = nullptr;
	Label *current_luminance_label = nullptr;
	HBoxContainer *reference_luminance_container = nullptr;
	Label *reference_luminance_label = nullptr;
	CheckBox *auto_ref_luminance_checkbox = nullptr;
	HSlider *reference_luminance_slider = nullptr;
	SpinBox *reference_luminance_spinbox = nullptr;
	HBoxContainer *max_luminance_container = nullptr;
	Label *max_luminance_label = nullptr;
	CheckBox *auto_max_luminance_checkbox = nullptr;
	HSlider *max_luminance_slider = nullptr;
	SpinBox *max_luminance_spinbox = nullptr;

	HDRSettings current_settings;

	void _request_checkbox_toggled(bool p_enabled);
	void _auto_ref_luminance_toggled(bool p_enabled);
	void _auto_max_luminance_toggled(bool p_enabled);
	void _reference_luminance_changed(double p_value);
	void _max_luminance_changed(double p_value);
	
	Array _create_settings_array();

protected:
	static void _bind_methods();

public:
	void update_from_settings(const HDRSettings &p_settings);
	HDRSettings get_current_settings() const { return current_settings; }

	HDROptionsPopup();
};
