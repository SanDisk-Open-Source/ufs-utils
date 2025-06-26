/* SanDisk ufs-utils utilities including Eye Monitor functionality from Qualcomm developed utilities.
* Copyright (C) 2025 SanDisk Corporation or its affiliates
*
* This program is free software; you can redistribute it and/or modify it under the terms of the
* GNU General Public License as published by the Free Software Foundation;
* either version 2 of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
* Link: https://spdx.org/licenses/GPL-2.0-or-later.html
*
* You should have received a copy of the GNU General Public License along with this program;
* if not, write to the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
* This file incorporates code from Qualcomm's EOM tool implementation,
* source: https://github.com/quic/ufs-tools/tree/main/ufs-cli
* Copyright (C) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* Originally licensed under the BSD-3-Clause-Clear license.
* List of conditions and disclaimer is available at: https://spdx.org/licenses/BSD-3-Clause-Clear.html
*
* This combined work is redistributed under the terms of the GNU General Public License v2.0 or later license.
*/
// SPDX-License-Identifier: GPL-2.0-or-later


#ifndef UFS_EMON_H_
#define UFS_EMON_H_


#define ALL_LANES 0
#define LANE0_SELECTOR 4
#define LANE1_SELECTOR 5
#define DEFAULT_TEST_COUNT 93

void ufs_emon_help(char *tool_name);
int do_emon(struct tool_options *opt);

#endif /*UFS_EMON_H_*/
