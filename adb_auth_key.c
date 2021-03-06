/*
 * Copyright (C) 2020 Simon Piriou. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/* Copy your public key here (located in ~/.android/adbkey.pub)
 * Must be 524 bytes long
 */
static const unsigned char key0[] = {
  0x40, 0x00, 0x00, 0x00, 0xe1, 0x04, 0x78, 0x6b, 0xdf, 0xc0, 0x2b, 0x2d,
  0xf9, 0x1f, 0xbd, 0xf7, 0xd3, 0xaf, 0x40, 0xa2, 0x93, 0x9a, 0xc0, 0x81,
  0x65, 0x54, 0x86, 0x25, 0xac, 0xe4, 0x7d, 0xc1, 0x06, 0xdd, 0x4e, 0xbc,
  0xe0, 0x3e, 0xbb, 0xd2, 0x2d, 0xf4, 0x91, 0x94, 0x23, 0x13, 0x07, 0x5a,
  0x3d, 0x26, 0x25, 0x45, 0xbf, 0xe0, 0x8b, 0x02, 0x8a, 0x5f, 0xcc, 0xfa,
  0x60, 0xf1, 0x44, 0x07, 0x8e, 0xfc, 0x50, 0x06, 0x23, 0xde, 0x1b, 0x43,
  0xc4, 0xa1, 0x06, 0x60, 0x3d, 0x21, 0x2f, 0x4c, 0xc5, 0x7b, 0x76, 0x1c,
  0xe6, 0x2f, 0x3f, 0xa6, 0x8c, 0x80, 0x7b, 0x1e, 0x60, 0x2c, 0xb2, 0x62,
  0xd6, 0x16, 0xd6, 0x84, 0x7a, 0x98, 0xac, 0xc6, 0x37, 0xe0, 0xb0, 0x6d,
  0x2e, 0xe8, 0x98, 0xb6, 0x36, 0x8e, 0x3f, 0x69, 0x96, 0x69, 0x2a, 0xd6,
  0x41, 0x35, 0xf5, 0x53, 0x6f, 0x82, 0x05, 0x26, 0x27, 0x92, 0x8b, 0xc3,
  0xb0, 0x5f, 0xeb, 0xab, 0x95, 0x24, 0x31, 0x2e, 0x8a, 0x9d, 0x56, 0x9d,
  0xcc, 0x1b, 0x78, 0x78, 0xa6, 0xfc, 0x6e, 0x48, 0x30, 0x66, 0x76, 0xaf,
  0xe0, 0xc3, 0x35, 0x34, 0xef, 0x64, 0x38, 0x3b, 0xd7, 0x9f, 0xf1, 0x59,
  0x14, 0x29, 0x25, 0x12, 0x89, 0x5b, 0xbc, 0x21, 0x96, 0x68, 0xae, 0xd3,
  0xaa, 0xdc, 0x5b, 0x5f, 0x44, 0x48, 0x5f, 0xa9, 0xe6, 0x35, 0xa6, 0xc0,
  0x71, 0x90, 0x01, 0xe0, 0x4f, 0xd7, 0xc7, 0x38, 0xd4, 0x8f, 0x68, 0xa0,
  0x19, 0x6b, 0x35, 0xb7, 0xbf, 0x67, 0xcf, 0xdb, 0x74, 0x4f, 0x5b, 0xaf,
  0x1f, 0x3b, 0x1d, 0x8c, 0x90, 0x53, 0x62, 0x18, 0x29, 0xcc, 0x8b, 0x29,
  0xb3, 0xee, 0x40, 0xc6, 0xd7, 0x02, 0x84, 0x95, 0xe9, 0xc1, 0x92, 0x83,
  0x9e, 0x54, 0x5a, 0x30, 0x7f, 0x6b, 0x4c, 0xe6, 0x51, 0x9e, 0xef, 0x72,
  0xf8, 0xde, 0x4e, 0x98, 0x74, 0xd8, 0xd2, 0x02, 0xb8, 0x81, 0xf0, 0xa4,
  0x16, 0xd1, 0x5f, 0x2a, 0x86, 0x2e, 0x30, 0xcd, 0xf2, 0x24, 0x57, 0xa0,
  0x38, 0x19, 0x19, 0xaa, 0xb0, 0x3a, 0xb2, 0x95, 0x67, 0x34, 0x06, 0x40,
  0xfd, 0xfb, 0x3a, 0x7f, 0xf8, 0x98, 0x06, 0x44, 0x80, 0xf9, 0x10, 0x38,
  0xc2, 0x9c, 0x8c, 0x32, 0x9b, 0xc7, 0x78, 0x6f, 0x51, 0xf8, 0xc1, 0x09,
  0x09, 0x32, 0x01, 0xe3, 0x72, 0xea, 0x7a, 0xef, 0x25, 0xc1, 0x61, 0x98,
  0x1f, 0x72, 0xc8, 0xb9, 0x88, 0x7e, 0x8d, 0x5c, 0xc4, 0x15, 0x96, 0x50,
  0x88, 0x81, 0xd3, 0x8b, 0x6a, 0x78, 0x5d, 0x1c, 0x59, 0x5d, 0x96, 0xcb,
  0xa0, 0x37, 0x4c, 0xa6, 0x54, 0x42, 0x7f, 0x6c, 0x56, 0xaa, 0xda, 0x3a,
  0x11, 0xd3, 0x19, 0xe1, 0x4a, 0xc3, 0x7b, 0x5e, 0xf5, 0x76, 0x79, 0x13,
  0x79, 0xe9, 0x50, 0x04, 0xc9, 0xcc, 0xbe, 0x7b, 0x4d, 0xcd, 0x7d, 0x59,
  0x2a, 0x4e, 0xb6, 0x76, 0x1f, 0x35, 0xec, 0xca, 0x87, 0xb6, 0x1f, 0x1f,
  0xc8, 0x4f, 0x52, 0x30, 0x12, 0x54, 0xb0, 0x9a, 0xd4, 0x91, 0x71, 0x31,
  0x5d, 0xcf, 0xa6, 0x84, 0x2f, 0x44, 0x8d, 0xdc, 0x7f, 0x11, 0xa4, 0xf0,
  0xcf, 0x05, 0xbb, 0x9b, 0x38, 0xc3, 0x13, 0x33, 0x53, 0x18, 0x76, 0x53,
  0x22, 0x5e, 0x4a, 0x91, 0x8e, 0x83, 0xae, 0x66, 0xe0, 0x91, 0x7d, 0x08,
  0x58, 0x79, 0x63, 0x0f, 0xd1, 0x41, 0xa3, 0x42, 0x53, 0x9a, 0xbb, 0x8e,
  0xfa, 0x3d, 0xd1, 0x9f, 0xb7, 0xe6, 0x04, 0x94, 0xd3, 0xfb, 0xf7, 0x21,
  0x9a, 0x5f, 0x55, 0x9b, 0x14, 0xaa, 0xc5, 0xf6, 0x3a, 0xd0, 0x7c, 0x31,
  0xe1, 0xe6, 0xab, 0x06, 0xf6, 0x52, 0x8b, 0x99, 0x11, 0xb2, 0x50, 0x64,
  0xf7, 0xdf, 0x84, 0xca, 0xb8, 0xd8, 0x02, 0x96, 0x04, 0xc6, 0x45, 0xe0,
  0xe9, 0x9e, 0x47, 0x7a, 0x2a, 0x5f, 0xff, 0x5c, 0x0b, 0x4f, 0x4a, 0xc1,
  0xec, 0xf4, 0x45, 0x7b, 0x01, 0x00, 0x01, 0x00
};

const unsigned char * const g_adb_public_keys[] = {
  key0,
  /* Add other keys here */
  0
};
