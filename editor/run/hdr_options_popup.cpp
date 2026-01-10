/**************************************************************************/
/*  hdr_options_popup.cpp                                                 */
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

#include "hdr_options_popup.h"

#include "editor/themes/editor_scale.h"
#include "scene/gui/separator.h"
#include "scene/main/scene_string_names.h"

void HDROptionsPopup::_bind_methods() {
	ADD_SIGNAL(MethodInfo("hdr_settings_changed", PropertyInfo(Variant::DICTIONARY, "settings")));
}

void HDROptionsPopup::_request_checkbox_toggled(bool p_enabled) {
	current_settings.requested = p_enabled;
	
	Dictionary settings;
	settings["requested"] = current_settings.requested;
	settings["auto_luminance"] = current_settings.auto_luminance;
	settings["reference_luminance"] = current_settings.reference_luminance;
	settings["max_luminance"] = current_settings.max_luminance;
	emit_signal(SNAME("hdr_settings_changed"), settings);
}

void HDROptionsPopup::_auto_luminance_checkbox_toggled(bool p_enabled) {
	current_settings.auto_luminance = p_enabled;
	
	if (p_enabled) {
		reference_luminance_container->hide();
		max_luminance_container->hide();
		current_settings.reference_luminance = -1.0;
		current_settings.max_luminance = -1.0;
	} else {
		reference_luminance_container->show();
		max_luminance_container->show();
		current_settings.reference_luminance = reference_luminance_slider->get_value();
		current_settings.max_luminance = max_luminance_slider->get_value();
	}
	
	Dictionary settings;
	settings["requested"] = current_settings.requested;
	settings["auto_luminance"] = current_settings.auto_luminance;
	settings["reference_luminance"] = current_settings.reference_luminance;
	settings["max_luminance"] = current_settings.max_luminance;
	emit_signal(SNAME("hdr_settings_changed"), settings);
}

void HDROptionsPopup::_reference_luminance_changed(double p_value) {
	// Keep slider and spinbox synchronized
	if (reference_luminance_slider->get_value() != p_value) {
		reference_luminance_slider->set_value(p_value);
	}
	if (reference_luminance_spinbox->get_value() != p_value) {
		reference_luminance_spinbox->set_value(p_value);
	}
	
	current_settings.reference_luminance = p_value;
	
	// Update max luminance slider minimum
	if (max_luminance_slider->get_value() < p_value) {
		max_luminance_slider->set_value(p_value);
		current_settings.max_luminance = p_value;
	}
	max_luminance_slider->set_min(p_value);
	max_luminance_spinbox->set_min(p_value);
	
	Dictionary settings;
	settings["requested"] = current_settings.requested;
	settings["auto_luminance"] = current_settings.auto_luminance;
	settings["reference_luminance"] = current_settings.reference_luminance;
	settings["max_luminance"] = current_settings.max_luminance;
	emit_signal(SNAME("hdr_settings_changed"), settings);
}

void HDROptionsPopup::_max_luminance_changed(double p_value) {
	// Keep slider and spinbox synchronized
	if (max_luminance_slider->get_value() != p_value) {
		max_luminance_slider->set_value(p_value);
	}
	if (max_luminance_spinbox->get_value() != p_value) {
		max_luminance_spinbox->set_value(p_value);
	}
	
	current_settings.max_luminance = p_value;
	
	// Update reference luminance slider maximum
	if (reference_luminance_slider->get_value() > p_value) {
		reference_luminance_slider->set_value(p_value);
		current_settings.reference_luminance = p_value;
	}
	reference_luminance_slider->set_max(p_value);
	reference_luminance_spinbox->set_max(p_value);
	
	Dictionary settings;
	settings["requested"] = current_settings.requested;
	settings["auto_luminance"] = current_settings.auto_luminance;
	settings["reference_luminance"] = current_settings.reference_luminance;
	settings["max_luminance"] = current_settings.max_luminance;
	emit_signal(SNAME("hdr_settings_changed"), settings);
}

void HDROptionsPopup::update_from_settings(const HDRSettings &p_settings) {
	current_settings = p_settings;
	
	// Update max color label
	if (p_settings.enabled && p_settings.current_max_luminance > 0) {
		max_color_label->set_text(vformat(TTR("Max Color Value: %.1f nits"), p_settings.current_max_luminance));
	} else {
		max_color_label->set_text(TTR("Max Color Value: N/A"));
	}
	
	// Update request checkbox
	request_checkbox->set_pressed_no_signal(p_settings.requested);
	
	// Show/hide elements based on state
	if (!p_settings.requested) {
		// SDR mode
		error_label->hide();
		luminance_container->hide();
	} else if (p_settings.requested && !p_settings.enabled) {
		// HDR requested but not enabled
		luminance_container->hide();
		error_label->show();
		error_label->set_text(p_settings.error_message);
	} else {
		// HDR enabled
		error_label->hide();
		luminance_container->show();
		
		auto_luminance_checkbox->set_pressed_no_signal(p_settings.auto_luminance);
		
		if (p_settings.auto_luminance) {
			reference_luminance_container->hide();
			max_luminance_container->hide();
		} else {
			reference_luminance_container->show();
			max_luminance_container->show();
			
			// Update slider values and ranges
			reference_luminance_slider->set_max(p_settings.current_max_luminance);
			reference_luminance_slider->set_value(p_settings.reference_luminance);
			reference_luminance_spinbox->set_max(p_settings.current_max_luminance);
			reference_luminance_spinbox->set_value(p_settings.reference_luminance);
			
			max_luminance_slider->set_min(p_settings.reference_luminance);
			max_luminance_slider->set_max(p_settings.current_max_luminance);
			max_luminance_slider->set_value(p_settings.max_luminance);
			max_luminance_spinbox->set_min(p_settings.reference_luminance);
			max_luminance_spinbox->set_max(p_settings.current_max_luminance);
			max_luminance_spinbox->set_value(p_settings.max_luminance);
		}
	}
}

HDROptionsPopup::HDROptionsPopup() {
	set_title(TTR("HDR Output Options"));
	
	VBoxContainer *vbox = memnew(VBoxContainer);
	add_child(vbox);
	vbox->set_custom_minimum_size(Size2(300, 0) * EDSCALE);
	
	// Max color value label
	max_color_label = memnew(Label);
	vbox->add_child(max_color_label);
	max_color_label->set_text(TTR("Max Color Value: N/A"));
	
	vbox->add_child(memnew(HSeparator));
	
	// Request HDR checkbox
	request_checkbox = memnew(CheckBox);
	vbox->add_child(request_checkbox);
	request_checkbox->set_text(TTR("Request HDR Output"));
	request_checkbox->connect(SceneStringName(toggled), callable_mp(this, &HDROptionsPopup::_request_checkbox_toggled));
	
	// Error label
	error_label = memnew(Label);
	vbox->add_child(error_label);
	error_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	error_label->add_theme_color_override(SceneStringName(font_color), Color(1, 0.5, 0.5));
	error_label->hide();
	
	// Luminance controls container
	luminance_container = memnew(HBoxContainer);
	vbox->add_child(luminance_container);
	luminance_container->set_v_size_flags(SIZE_EXPAND_FILL);
	
	VBoxContainer *luminance_vbox = memnew(VBoxContainer);
	luminance_container->add_child(luminance_vbox);
	luminance_vbox->set_h_size_flags(SIZE_EXPAND_FILL);
	
	vbox->add_child(memnew(HSeparator));
	
	// Auto luminance checkbox
	auto_luminance_checkbox = memnew(CheckBox);
	luminance_vbox->add_child(auto_luminance_checkbox);
	auto_luminance_checkbox->set_text(TTR("Automatic Luminance"));
	auto_luminance_checkbox->set_pressed(true);
	auto_luminance_checkbox->connect(SceneStringName(toggled), callable_mp(this, &HDROptionsPopup::_auto_luminance_checkbox_toggled));
	
	luminance_vbox->add_child(memnew(HSeparator));
	
	// Reference luminance controls
	reference_luminance_container = memnew(HBoxContainer);
	luminance_vbox->add_child(reference_luminance_container);
	
	VBoxContainer *ref_lum_vbox = memnew(VBoxContainer);
	reference_luminance_container->add_child(ref_lum_vbox);
	ref_lum_vbox->set_h_size_flags(SIZE_EXPAND_FILL);
	
	reference_luminance_label = memnew(Label);
	ref_lum_vbox->add_child(reference_luminance_label);
	reference_luminance_label->set_text(TTR("Reference Luminance (nits)"));
	
	HBoxContainer *ref_lum_controls = memnew(HBoxContainer);
	ref_lum_vbox->add_child(ref_lum_controls);
	
	reference_luminance_slider = memnew(HSlider);
	ref_lum_controls->add_child(reference_luminance_slider);
	reference_luminance_slider->set_h_size_flags(SIZE_EXPAND_FILL);
	reference_luminance_slider->set_min(10.0);
	reference_luminance_slider->set_max(2000.0);
	reference_luminance_slider->set_step(10.0);
	reference_luminance_slider->set_value(100.0);
	reference_luminance_slider->connect(SceneStringName(value_changed), callable_mp(this, &HDROptionsPopup::_reference_luminance_changed));
	
	reference_luminance_spinbox = memnew(SpinBox);
	ref_lum_controls->add_child(reference_luminance_spinbox);
	reference_luminance_spinbox->set_min(10.0);
	reference_luminance_spinbox->set_max(2000.0);
	reference_luminance_spinbox->set_step(10.0);
	reference_luminance_spinbox->set_value(100.0);
	reference_luminance_spinbox->connect(SceneStringName(value_changed), callable_mp(this, &HDROptionsPopup::_reference_luminance_changed));
	reference_luminance_spinbox->set_custom_minimum_size(Size2(80, 0) * EDSCALE);
	
	// Max luminance controls
	max_luminance_container = memnew(HBoxContainer);
	luminance_vbox->add_child(max_luminance_container);
	
	VBoxContainer *max_lum_vbox = memnew(VBoxContainer);
	max_luminance_container->add_child(max_lum_vbox);
	max_lum_vbox->set_h_size_flags(SIZE_EXPAND_FILL);
	
	max_luminance_label = memnew(Label);
	max_lum_vbox->add_child(max_luminance_label);
	max_luminance_label->set_text(TTR("Max Luminance (nits)"));
	
	HBoxContainer *max_lum_controls = memnew(HBoxContainer);
	max_lum_vbox->add_child(max_lum_controls);
	
	max_luminance_slider = memnew(HSlider);
	max_lum_controls->add_child(max_luminance_slider);
	max_luminance_slider->set_h_size_flags(SIZE_EXPAND_FILL);
	max_luminance_slider->set_min(100.0);
	max_luminance_slider->set_max(2000.0);
	max_luminance_slider->set_step(10.0);
	max_luminance_slider->set_value(1000.0);
	max_luminance_slider->connect(SceneStringName(value_changed), callable_mp(this, &HDROptionsPopup::_max_luminance_changed));
	
	max_luminance_spinbox = memnew(SpinBox);
	max_lum_controls->add_child(max_luminance_spinbox);
	max_luminance_spinbox->set_min(100.0);
	max_luminance_spinbox->set_max(2000.0);
	max_luminance_spinbox->set_step(10.0);
	max_luminance_spinbox->set_value(1000.0);
	max_luminance_spinbox->connect(SceneStringName(value_changed), callable_mp(this, &HDROptionsPopup::_max_luminance_changed));
	max_luminance_spinbox->set_custom_minimum_size(Size2(80, 0) * EDSCALE);
}
