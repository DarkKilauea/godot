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
	ADD_SIGNAL(MethodInfo("hdr_settings_changed", PropertyInfo(Variant::ARRAY, "settings")));
}

void HDROptionsPopup::_request_checkbox_toggled(bool p_enabled) {
	current_settings.requested = p_enabled;
	
	// Send settings using Array instead of Dictionary
	emit_signal(SNAME("hdr_settings_changed"), _create_settings_array());
}

void HDROptionsPopup::_auto_ref_luminance_toggled(bool p_enabled) {
	current_settings.auto_ref_luminance = p_enabled;
	
	reference_luminance_slider->set_editable(!p_enabled);
	reference_luminance_spinbox->set_editable(!p_enabled);
	
	if (!p_enabled) {
		// Use the last automatic value when switching to manual
		reference_luminance_slider->set_value(current_settings.current_ref_luminance);
		reference_luminance_spinbox->set_value(current_settings.current_ref_luminance);
		current_settings.reference_luminance = current_settings.current_ref_luminance;
	} else {
		current_settings.reference_luminance = -1.0;
	}
	
	emit_signal(SNAME("hdr_settings_changed"), _create_settings_array());
}

void HDROptionsPopup::_auto_max_luminance_toggled(bool p_enabled) {
	current_settings.auto_max_luminance = p_enabled;
	
	max_luminance_slider->set_editable(!p_enabled);
	max_luminance_spinbox->set_editable(!p_enabled);
	
	if (!p_enabled) {
		// Use the last automatic value when switching to manual
		max_luminance_slider->set_value(current_settings.current_max_luminance);
		max_luminance_spinbox->set_value(current_settings.current_max_luminance);
		current_settings.max_luminance = current_settings.current_max_luminance;
	} else {
		current_settings.max_luminance = -1.0;
	}
	
	emit_signal(SNAME("hdr_settings_changed"), _create_settings_array());
}

void HDROptionsPopup::_reference_luminance_changed(double p_value) {
	if (current_settings.auto_ref_luminance) {
		return; // Ignore changes when in auto mode
	}
	
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
	
	emit_signal(SNAME("hdr_settings_changed"), _create_settings_array());
}

void HDROptionsPopup::_max_luminance_changed(double p_value) {
	if (current_settings.auto_max_luminance) {
		return; // Ignore changes when in auto mode
	}
	
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
	
	emit_signal(SNAME("hdr_settings_changed"), _create_settings_array());
}

Array HDROptionsPopup::_create_settings_array() {
	Array settings;
	settings.append(current_settings.requested);
	settings.append(current_settings.auto_ref_luminance);
	settings.append(current_settings.auto_max_luminance);
	settings.append(current_settings.reference_luminance);
	settings.append(current_settings.max_luminance);
	return settings;
}

void HDROptionsPopup::update_from_settings(const HDRSettings &p_settings) {
	current_settings = p_settings;
	
	// Update max color label - this is a linear multiplier, not nits
	if (p_settings.enabled && p_settings.max_color_value > 0) {
		max_color_label->set_text(vformat(TTR("Max Color Value: %.2fx"), p_settings.max_color_value));
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
		
		// Set error message based on error code
		String error_msg;
		if (p_settings.error_code == 1) {
			error_msg = TTR("Display Server does not support HDR output.");
		} else if (p_settings.error_code == 2) {
			error_msg = TTR("Window does not support HDR output. Please ensure that your window is positioned on a screen that is currently in HDR mode.");
		} else {
			error_msg = TTR("HDR output is not available.");
		}
		error_label->set_text(error_msg);
	} else {
		// HDR enabled
		error_label->hide();
		luminance_container->show();
		
		// Update current luminance info
		current_luminance_label->set_text(vformat(TTR("Current: Ref %.0f nits, Max %.0f nits"), 
			p_settings.current_ref_luminance, p_settings.current_max_luminance));
		
		// Update auto checkboxes
		auto_ref_luminance_checkbox->set_pressed_no_signal(p_settings.auto_ref_luminance);
		auto_max_luminance_checkbox->set_pressed_no_signal(p_settings.auto_max_luminance);
		
		// Update sliders/spinboxes based on auto state
		reference_luminance_slider->set_editable(!p_settings.auto_ref_luminance);
		reference_luminance_spinbox->set_editable(!p_settings.auto_ref_luminance);
		max_luminance_slider->set_editable(!p_settings.auto_max_luminance);
		max_luminance_spinbox->set_editable(!p_settings.auto_max_luminance);
		
		// Update slider values and ranges
		reference_luminance_slider->set_max(p_settings.current_max_luminance);
		reference_luminance_spinbox->set_max(p_settings.current_max_luminance);
		
		float ref_value = p_settings.auto_ref_luminance ? p_settings.current_ref_luminance : p_settings.reference_luminance;
		reference_luminance_slider->set_value(ref_value);
		reference_luminance_spinbox->set_value(ref_value);
		
		max_luminance_slider->set_min(ref_value);
		max_luminance_spinbox->set_min(ref_value);
		max_luminance_slider->set_max(p_settings.current_max_luminance);
		max_luminance_spinbox->set_max(p_settings.current_max_luminance);
		
		float max_value = p_settings.auto_max_luminance ? p_settings.current_max_luminance : p_settings.max_luminance;
		max_luminance_slider->set_value(max_value);
		max_luminance_spinbox->set_value(max_value);
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
	luminance_container->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	
	VBoxContainer *luminance_vbox = memnew(VBoxContainer);
	luminance_container->add_child(luminance_vbox);
	luminance_vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	
	vbox->add_child(memnew(HSeparator));
	
	// Current luminance info (shown when HDR is enabled)
	current_luminance_label = memnew(Label);
	luminance_vbox->add_child(current_luminance_label);
	current_luminance_label->set_text(TTR("Current: Ref 100 nits, Max 1000 nits"));
	
	luminance_vbox->add_child(memnew(HSeparator));
	
	// Reference luminance controls
	reference_luminance_container = memnew(HBoxContainer);
	luminance_vbox->add_child(reference_luminance_container);
	
	VBoxContainer *ref_lum_vbox = memnew(VBoxContainer);
	reference_luminance_container->add_child(ref_lum_vbox);
	ref_lum_vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	
	HBoxContainer *ref_lum_header = memnew(HBoxContainer);
	ref_lum_vbox->add_child(ref_lum_header);
	
	reference_luminance_label = memnew(Label);
	ref_lum_header->add_child(reference_luminance_label);
	reference_luminance_label->set_text(TTR("Reference Luminance (nits)"));
	reference_luminance_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	
	auto_ref_luminance_checkbox = memnew(CheckBox);
	ref_lum_header->add_child(auto_ref_luminance_checkbox);
	auto_ref_luminance_checkbox->set_text(TTR("Auto"));
	auto_ref_luminance_checkbox->set_pressed(true);
	auto_ref_luminance_checkbox->connect(SceneStringName(toggled), callable_mp(this, &HDROptionsPopup::_auto_ref_luminance_toggled));
	
	HBoxContainer *ref_lum_controls = memnew(HBoxContainer);
	ref_lum_vbox->add_child(ref_lum_controls);
	
	reference_luminance_slider = memnew(HSlider);
	ref_lum_controls->add_child(reference_luminance_slider);
	reference_luminance_slider->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	reference_luminance_slider->set_min(10.0);
	reference_luminance_slider->set_max(2000.0);
	reference_luminance_slider->set_step(10.0);
	reference_luminance_slider->set_value(100.0);
	reference_luminance_slider->set_editable(false);
	reference_luminance_slider->connect(SceneStringName(value_changed), callable_mp(this, &HDROptionsPopup::_reference_luminance_changed));
	
	reference_luminance_spinbox = memnew(SpinBox);
	ref_lum_controls->add_child(reference_luminance_spinbox);
	reference_luminance_spinbox->set_min(10.0);
	reference_luminance_spinbox->set_max(2000.0);
	reference_luminance_spinbox->set_step(10.0);
	reference_luminance_spinbox->set_value(100.0);
	reference_luminance_spinbox->set_editable(false);
	reference_luminance_spinbox->connect(SceneStringName(value_changed), callable_mp(this, &HDROptionsPopup::_reference_luminance_changed));
	reference_luminance_spinbox->set_custom_minimum_size(Size2(80, 0) * EDSCALE);
	
	// Max luminance controls
	max_luminance_container = memnew(HBoxContainer);
	luminance_vbox->add_child(max_luminance_container);
	
	VBoxContainer *max_lum_vbox = memnew(VBoxContainer);
	max_luminance_container->add_child(max_lum_vbox);
	max_lum_vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	
	HBoxContainer *max_lum_header = memnew(HBoxContainer);
	max_lum_vbox->add_child(max_lum_header);
	
	max_luminance_label = memnew(Label);
	max_lum_header->add_child(max_luminance_label);
	max_luminance_label->set_text(TTR("Max Luminance (nits)"));
	max_luminance_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	
	auto_max_luminance_checkbox = memnew(CheckBox);
	max_lum_header->add_child(auto_max_luminance_checkbox);
	auto_max_luminance_checkbox->set_text(TTR("Auto"));
	auto_max_luminance_checkbox->set_pressed(true);
	auto_max_luminance_checkbox->connect(SceneStringName(toggled), callable_mp(this, &HDROptionsPopup::_auto_max_luminance_toggled));
	
	HBoxContainer *max_lum_controls = memnew(HBoxContainer);
	max_lum_vbox->add_child(max_lum_controls);
	
	max_luminance_slider = memnew(HSlider);
	max_lum_controls->add_child(max_luminance_slider);
	max_luminance_slider->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	max_luminance_slider->set_min(100.0);
	max_luminance_slider->set_max(2000.0);
	max_luminance_slider->set_step(10.0);
	max_luminance_slider->set_value(1000.0);
	max_luminance_slider->set_editable(false);
	max_luminance_slider->connect(SceneStringName(value_changed), callable_mp(this, &HDROptionsPopup::_max_luminance_changed));
	
	max_luminance_spinbox = memnew(SpinBox);
	max_lum_controls->add_child(max_luminance_spinbox);
	max_luminance_spinbox->set_min(100.0);
	max_luminance_spinbox->set_max(2000.0);
	max_luminance_spinbox->set_step(10.0);
	max_luminance_spinbox->set_value(1000.0);
	max_luminance_spinbox->set_editable(false);
	max_luminance_spinbox->connect(SceneStringName(value_changed), callable_mp(this, &HDROptionsPopup::_max_luminance_changed));
	max_luminance_spinbox->set_custom_minimum_size(Size2(80, 0) * EDSCALE);
}
