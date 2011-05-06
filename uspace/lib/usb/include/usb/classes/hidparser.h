/*
 * Copyright (c) 2011 Matej Klonfar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup libusb
 * @{
 */
/** @file
 * USB HID report descriptor and report data parser
 */
#ifndef LIBUSB_HIDPARSER_H_
#define LIBUSB_HIDPARSER_H_

#include <stdint.h>
#include <adt/list.h>
#include <usb/classes/hid_report_items.h>
#include <usb/classes/hidpath.h>
#include <usb/classes/hidtypes.h>
#include <usb/classes/hiddescriptor.h>


/*
 * Input report parser functions
 */
/** */
int usb_hid_parse_report(const usb_hid_report_t *report, const uint8_t *data, 
                         size_t size, uint8_t *report_id);

/** */
size_t usb_hid_report_input_length(const usb_hid_report_t *report,
	usb_hid_report_path_t *path, int flags);

/*
 * Output report parser functions
 */
/** Allocates output report buffer*/
uint8_t *usb_hid_report_output(usb_hid_report_t *report, size_t *size, 
                               uint8_t report_id);

/** Frees output report buffer*/
void usb_hid_report_output_free(uint8_t *output);

/** Returns size of output for given usage path */
size_t usb_hid_report_output_size(usb_hid_report_t *report,
                                  usb_hid_report_path_t *path, int flags);

/** Makes the output report buffer by translated given data */
int usb_hid_report_output_translate(usb_hid_report_t *report, uint8_t report_id, 
                                    uint8_t *buffer, size_t size);

/** */
usb_hid_report_field_t *usb_hid_report_get_sibling(usb_hid_report_t *report, 
                                                   usb_hid_report_field_t *field, 
                                                   usb_hid_report_path_t *path, 
                                                   int flags, 
                                                   usb_hid_report_type_t type);

/** */
uint8_t usb_hid_report_get_report_id(usb_hid_report_t *report, 
                                     uint8_t report_id, 
                                     usb_hid_report_type_t type);

#endif
/**
 * @}
 */
