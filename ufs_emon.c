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


#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <malloc.h>

#include "options.h"
#include "ufs.h"
#include "unipro.h"
#include "ufs_cmds.h"
#include "ufs_emon.h"

/*4MB*/
#define FILL_DATE_SIZE 0x400000

/*4KB*/
#define DATA_MEM_ALIGN_SIZE 0x10000

#define RX_EYEMON_CAPABILITY			0x00F1
#define RX_EYEMON_TIMING_MAX_STEPS_CAPABILITY	0x00F2
#define RX_EYEMON_TIMING_MAX_OFFSET_CAPABILITY	0x00F3
#define RX_EYEMON_VOLTAGE_MAX_STEPS_CAPABILITY	0x00F4
#define RX_EYEMON_VOLTAGE_MAX_OFFSET_CAPABILITY 0x00F5
#define RX_EYEMON_ENABLE			0x00F6
#define RX_EYEMON_TIMING_STEPS			0x00F7
#define RX_EYEMON_VOLTAGE_STEPS			0x00F8
#define RX_EYEMON_TARGET_TEST_COUNT		0x00F9
#define RX_EYEMON_TESTED_COUNT			0x00FA
#define RX_EYEMON_ERROR_COUNT			0x00FB
#define RX_EYEMON_START				0x00FC

#define PA_PWRMODE				0x1571
#define PA_TXHSADAPTTYPE			0x15D4

#define RX_EYEMON_START_MASK			0x1

#define QCOM_DME_VS_UNIPRO_STATE		0xD000

#define QCOM_DME_VS_UNIPRO_STATE_MASK		0x7
#define QCOM_DME_VS_UNIPRO_STATE_LINK_UP	0x2

#define LANE0 "Line 0"
#define LANE1 "Line 1"

#define SELECT_RX(l) ((l) + 4)
#define SELECT_TX(l) (l)

#define PA_REFRESH_ADAPT	 0x00
#define PA_INITIAL_ADAPT	0x01
#define PA_NO_ADAPT	0x03

#define EOM_PHY_ERROR_COUNT_THRESHOLD	0x3C
#define EOM_DIRECTION_SHIFT		0x6
#define EOM_STEP_MASK			0x3F

#define STRING_BUFFER_SIZE		0x24
struct eom_result {
	int lane;
	int timing;
	int volt;
	int error_cnt;
};

struct EOMData {
	int timing_max_steps;
	int timing_max_offset;
	int voltage_max_steps;
	int voltage_max_offset;
	int data_cnt;
	int num_lanes;
	int local_peer;
	struct eom_result *er;
} eom_data;

#define MANUFACTURER_NAME_OFFSET		0x14
#define PRODUCT_NAME_OFFSET			0x15
#define PRODUCT_REVISION_LEVEL_OFFSET		0x2A

#define MANUFACTURER_NAME_STRING_DESC_SIZE	18
#define PRODUCT_NAME_STRING_DESC_SIZE		34
#define PRODUCT_REVISION_LEVEL_STRING_DESC_SIZE	10

static int eom_result_count;

static int parse_string_desc(__u8 *buf, char *string)
{
	int len, i, j;

	if (buf == NULL || string == NULL)
		return ERROR;

	/* bLength */
	len = buf[0];

	for (i = 2, j = 0; i < len; i++) {
		if (buf[i])
			string[j++] = (char)buf[i];
	}

	strcat(string, "\0");

	return OK;
}

static int get_device_info(int fd, char *mname, char *pname, char *pversion)
{
	int mname_idx, pname_idx, pver_idx, rc;
	struct ufs_bsg_request bsg_req = {0};
	struct ufs_bsg_reply bsg_rsp = {0};
	__u8 desc_buf[QUERY_DESC_MAX_SIZE] = {0};
	char string_buf[QUERY_DESC_MAX_SIZE];

	rc = do_read_desc(fd, &bsg_req, &bsg_rsp,
			  QUERY_DESC_IDN_DEVICE,
			  0, QUERY_DESC_MAX_SIZE,
			  desc_buf);
	if (rc) {
		print_error("Failed to read Device Descriptor\n");
		goto out;
	}

	mname_idx = desc_buf[MANUFACTURER_NAME_OFFSET];
	pname_idx = desc_buf[PRODUCT_NAME_OFFSET];
	pver_idx = desc_buf[PRODUCT_REVISION_LEVEL_OFFSET];

	rc = do_read_desc(fd, &bsg_req, &bsg_rsp,
			  QUERY_DESC_IDN_STRING,
			  mname_idx, QUERY_DESC_MAX_SIZE,
			  desc_buf);
	if (rc) {
		print_error("Failed to read Manufacturer Name String Descriptor\n");
		goto out;
	}
	memset(string_buf, 0, STRING_BUFFER_SIZE);
	parse_string_desc(desc_buf, string_buf);
	strcpy(mname, string_buf);

	rc = do_read_desc(fd, &bsg_req, &bsg_rsp,
			  QUERY_DESC_IDN_STRING,
			  pname_idx, QUERY_DESC_MAX_SIZE,
			  desc_buf);
	if (rc) {
		print_error("Failed to read Product Name String Descriptor\n");
		goto out;
	}
	memset(string_buf, 0, STRING_BUFFER_SIZE);
	parse_string_desc(desc_buf, string_buf);
	strcpy(pname, string_buf);

	rc = do_read_desc(fd, &bsg_req, &bsg_rsp,
			  QUERY_DESC_IDN_STRING,
			  pver_idx, QUERY_DESC_MAX_SIZE,
			  desc_buf);
	if (rc) {
		print_error("Failed to read Product Revision Level String Descriptor\n");
		goto out;
	}
	memset(string_buf, 0, STRING_BUFFER_SIZE);
	parse_string_desc(desc_buf, string_buf);
	strcpy(pversion, string_buf);

out:
	return rc;
}

static int config_eom(int fd, int peer, int lane, int timing, int volt,
		      int target_count)
{
	int rc = OK;

	/* Enable Eye Monitor */
	rc = ufshcd_dme_set_attr(fd,
				 UIC_ARG_MIB_SEL(RX_EYEMON_ENABLE,
				 SELECT_RX(lane)),
				 ATTR_SET_NOR, 1, peer);
	if (rc) {
		print_error("Failed to set RX_EYEMON_Enable\n");
		return rc;
	}

	/* Config Eye Monitor timing steps */
	rc = ufshcd_dme_set_attr(fd, UIC_ARG_MIB_SEL(RX_EYEMON_TIMING_STEPS,
				 SELECT_RX(lane)),
				 ATTR_SET_NOR, timing, peer);
	if (rc) {
		print_error("Failed to set RX_EYEMON_Timing_Steps\n");
		return rc;
	}

	/* Config Eye Monitor voltage steps */
	rc = ufshcd_dme_set_attr(fd, UIC_ARG_MIB_SEL(RX_EYEMON_VOLTAGE_STEPS,
				 SELECT_RX(lane)),
				 ATTR_SET_NOR, volt, peer);
	if (rc) {
		print_error("Failed to set RX_EYEMON_Voltage_Steps\n");
		return rc;
	}

	/* Config Eye Monitor target test count */
	rc = ufshcd_dme_set_attr(fd, UIC_ARG_MIB_SEL(RX_EYEMON_TARGET_TEST_COUNT,
				 SELECT_RX(lane)),
				 ATTR_SET_NOR, target_count, peer);
	if (rc) {
		print_error("Failed to set RX_EYEMON_Target_Test_Count\n");
		return rc;
	}

	/* Select NO_ADAPT */
	rc = ufshcd_dme_set_attr(fd, UIC_ARG_MIB_SEL(PA_TXHSADAPTTYPE, 0),
				  ATTR_SET_NOR, PA_NO_ADAPT, 0);
	if (rc) {
		print_error("Failed to set NO_ADAPT\n");
		return rc;
	}

	/* Do a Power Mode Change to Fast Mode to apply NO_ADAPT and also
	 * trigger a RCT to kick start EOM */
	rc = ufshcd_dme_set_attr(fd, UIC_ARG_MIB_SEL(PA_PWRMODE, 0),
				 ATTR_SET_NOR, 0x11, 0);
	if (rc) {
		print_error("Failed to trigger RCT\n");
		return rc;
	}

	/* Poll UniPro State to confirm PMC is done. */
	rc = ufshcd_dme_get_attr(fd,
					 UIC_ARG_MIB_SEL(QCOM_DME_VS_UNIPRO_STATE, 0),
					 0);
	if ((rc < 0) ||
	    !((rc & QCOM_DME_VS_UNIPRO_STATE_MASK) == QCOM_DME_VS_UNIPRO_STATE_LINK_UP)) {
	/* QCOM_DME_VS_UNIPRO_STATE not supported? Delay a bit to make sure PMC is completed */
		print_error("Failed to get QCOM_DME_VS_UNIPRO_STATE, maybe not supported?\n");
		usleep(200000);
	}

	return OK;
}

static int eom_scan(int fd, int peer, int lane,
		    int timing, int volt, int target_count,
		    int fill_data_fd, __u8 *fill_buf)
{
	struct EOMData *data = &eom_data;
	int voltage_direction, timing_direction;
	int voltage_steps, timing_steps;
	int eom_start, eom_tested_count, eom_error_count;
	int rc;

	if (volt < 0) {
		voltage_direction = 1;
		voltage_steps = (voltage_direction << EOM_DIRECTION_SHIFT) |
				(-volt & EOM_STEP_MASK);
	} else {
		voltage_direction = 0;
		voltage_steps = (voltage_direction << EOM_DIRECTION_SHIFT) |
				(volt & EOM_STEP_MASK);
	}

	if (timing < 0) {
		timing_direction = 1;
		timing_steps = (timing_direction << EOM_DIRECTION_SHIFT) |
				(-timing & EOM_STEP_MASK);
	} else {
		timing_direction = 0;
		timing_steps = (timing_direction << EOM_DIRECTION_SHIFT) |
				(timing & EOM_STEP_MASK);
	}

	rc = config_eom(fd, peer, lane, timing_steps, voltage_steps,
			target_count);
	if (rc) {
		print_error("Failed to configure EOM.\n");
		return rc;
	}

repeat_eom_scan:
	if (peer) {
		/* Write to excercise peer device's Rx */
		rc = pwrite(fill_data_fd, fill_buf, FILL_DATE_SIZE, 0);
		if (rc < 0) {
			print_error("Failed to write tmp file\n");
			return ERROR;
		}
	} else {
		/* Read to excercise local device's Rx */
		rc = pread(fill_data_fd, fill_buf, FILL_DATE_SIZE, 0);
		if (rc < 0){
			print_error("Failed to read tmp file\n");
			return ERROR;
		}
	}

	/* Get RX_EYEMON_Start */
	eom_start = ufshcd_dme_get_attr(fd, UIC_ARG_MIB_SEL(RX_EYEMON_START,
					SELECT_RX(lane)),
					peer);
	if (eom_start < 0) {
		print_error("Failed to get RX_EYEMON_START, eom_start = %d\n",
			    eom_start);
		return ERROR;
	}

	/* EOM has not yet stopped */
	if (eom_start & RX_EYEMON_START_MASK)
		goto repeat_eom_scan;

	/* Get RX_EYEMON_Tested_Count */
	eom_tested_count = ufshcd_dme_get_attr(fd,
					       UIC_ARG_MIB_SEL(RX_EYEMON_TESTED_COUNT,
					       SELECT_RX(lane)),
					       peer);
	if (eom_tested_count < 0) {
		print_error("Failed to get RX_EYEMON_Tested_Count\n");
		return ERROR;
	}

	/* Get RX_EYEMON_Error_Count */
	eom_error_count =  ufshcd_dme_get_attr(fd,
					       UIC_ARG_MIB_SEL(RX_EYEMON_ERROR_COUNT,
					       SELECT_RX(lane)),
					       peer);
	if (eom_error_count < 0) {
		print_error("Failed to get RX_EYEMON_Error_Count\n");
		return ERROR;
	}

	/* EOM has stopped, good to log results */
	if (eom_tested_count >= target_count || eom_error_count >= EOM_PHY_ERROR_COUNT_THRESHOLD) {
			printf("lane: %d timing: %d voltage: %d error count: %d"
				"[tested_count: %d]\n",
				lane, timing, volt,
				eom_error_count, eom_tested_count);

		data->er[data->data_cnt].lane = lane;
		data->er[data->data_cnt].timing = timing;
		data->er[data->data_cnt].volt = volt;
		data->er[data->data_cnt].error_cnt = eom_error_count;
		data->data_cnt ++;
		if (data->data_cnt > eom_result_count) {
			print_error("The count of data exceeds the maximum %d of the device\n",
				    eom_result_count);
			return ERROR;
		}

		return OK;
	}

	/* EOM is running or has not yet started */
	goto repeat_eom_scan;
}

static int generate_eom_report(int fd, char *eom_file, struct EOMData *data)
{
	char mname[MANUFACTURER_NAME_STRING_DESC_SIZE];
	char pname[PRODUCT_NAME_STRING_DESC_SIZE];
	char pver[PRODUCT_REVISION_LEVEL_STRING_DESC_SIZE];
	int i, rc;
	FILE *file;

	rc = get_device_info(fd, mname, pname, pver);
	if (rc)
		return rc;

	file = fopen(eom_file, "w");
	if (!file) {
		print_error("Failed to create EOM result file %s\n", eom_file);
		return ERROR;
	}

	fprintf(file, "UFS %s Side Eye Monitor Start\n",
		data->local_peer ? "Device" : "Host");
	fprintf(file, "- - - - UFS INQUIRY ID: %s %s %s\n", mname, pname, pver);
	fprintf(file, "EOM Capabilities:\n");
	fprintf(file, "TimingMaxSteps %d TimingMaxOffset %d\n",
		data->timing_max_steps, data->timing_max_offset);
	fprintf(file, "VoltageMaxSteps %d VoltageMaxOffset %d\n\n",
		data->voltage_max_steps, data->voltage_max_offset);

	for (i = 0; i < data->data_cnt; i++)
		fprintf(file, "lane: %d timing: %d voltage: %d error count: %d\n",
				data->er[i].lane, data->er[i].timing,
				data->er[i].volt, data->er[i].error_cnt);

	fclose(file);
	printf("EOM results saved to %s\n", eom_file);

	return OK;
}

int do_emon(struct tool_options *opt)
{
	int rc = ERROR;
	struct EOMData *data = &eom_data;
	int fd;
	int eye_cap, l, n, t, v;
	int fill_data_fd;
	__u8 *fill_buf = 0;
	int gen_id_selector;
	struct timespec ts_start, ts_end;
	char eom_file_name[256], lane_str[8];
	size_t eom_result_size;
	char output_file[PATH_MAX] = {0};

	if  (opt->selector == ALL_LANES) {
		gen_id_selector = LANE0_SELECTOR;
		data->num_lanes = 2;
	} else {
		gen_id_selector = opt->selector;
		data->num_lanes = 1;
	}
	data->er = 0;
	data->local_peer = opt->idn;

	fd = open(opt->path, O_RDWR);
	if (fd < 0) {
		perror("Device open");
		return ERROR;
	}

	fill_data_fd = open("fill_file", O_RDWR | O_DIRECT | O_CREAT,
			    S_IWUSR | S_IRUSR);

	if (fill_data_fd < 0) {
		perror("Fill data open");
		rc = ERROR;
		goto out;
	}

	/* Allocate buffer for I/O */
	fill_buf = memalign(DATA_MEM_ALIGN_SIZE, FILL_DATE_SIZE);
	if (!fill_buf) {
		print_error("Failed to allocate fill_buf");
		rc = ERROR;
		goto out;
	}

	rc = pwrite(fill_data_fd, fill_buf, FILL_DATE_SIZE, 0);
	if (rc < 0) {
		print_error("Failed to write fill data");
		goto out;
	}

	eye_cap = ufshcd_dme_get_attr(fd,
				      UIC_ARG_MIB_SEL(RX_EYEMON_CAPABILITY,
				      gen_id_selector), opt->idn);
	if (eye_cap == ERROR) {
		print_error("Read RX_EYEMON_CAPABILITY Failed");
		goto out;
	}

	if (!eye_cap) {
		print_error("Eye monitor doesn't supported");
		goto out;
	}

	data->timing_max_offset = ufshcd_dme_get_attr(fd,
						      UIC_ARG_MIB_SEL(
						      RX_EYEMON_TIMING_MAX_OFFSET_CAPABILITY,
						      gen_id_selector), opt->idn);

	if (data->timing_max_offset == ERROR) {
		print_error("Read RX_EYEMON_TIMING_MAX_OFFSET_CAPABILITY Failed");
		goto out;
	}

	data->voltage_max_offset = ufshcd_dme_get_attr(fd,
						       UIC_ARG_MIB_SEL(
						       RX_EYEMON_VOLTAGE_MAX_OFFSET_CAPABILITY,
						       gen_id_selector), opt->idn);

	if (data->voltage_max_offset == ERROR) {
		print_error("Read RX_EYEMON_VOLTAGE_MAX_OFFSET_CAPABILITY Failed");
		goto out;
	}

	data->timing_max_steps = ufshcd_dme_get_attr(fd,
						     UIC_ARG_MIB_SEL(
						     RX_EYEMON_TIMING_MAX_STEPS_CAPABILITY,
						     gen_id_selector), opt->idn);

	if (data->timing_max_steps == ERROR) {
		print_error("Read RX_EYEMON_TIMING_MAX_STEPS_CAPABILITY Failed");
		goto out;
	}

	if ((data->timing_max_steps > opt->max_time) && (opt->max_time > 0))
		data->timing_max_steps = opt->max_time;


	data->voltage_max_steps = ufshcd_dme_get_attr(fd,
						      UIC_ARG_MIB_SEL(
						      RX_EYEMON_VOLTAGE_MAX_STEPS_CAPABILITY,
						      gen_id_selector), opt->idn);

	if (data->voltage_max_steps == ERROR) {
		print_error("Read RX_EYEMON_VOLTAGE_MAX_STEPS_CAPABILITY Failed");
		goto out;
	}

	if ((data->voltage_max_steps > opt->max_vol) && (opt->max_vol > 0))
		data->voltage_max_steps = opt->max_vol;

	/* EOM result file name rule:local/peer_lane_0/_1_targetestcount.eom */
	if (opt->selector == LANE0_SELECTOR ||
	    opt->selector == LANE1_SELECTOR) {
		snprintf(lane_str, sizeof(lane_str), "%d",
			 opt->selector - LANE0_SELECTOR);
	}

	snprintf(eom_file_name, sizeof(eom_file_name), "%s_lane_%s_ttc_%d.eom",
		 opt->idn ? "peer" : "local",
		 (opt->selector == ALL_LANES) ? "0_1" : lane_str,
		 opt->test_count);

	if (opt->data) {
		//strncpy(output_file, "opt->data, eom_file_name, PATH_MAX - 1);"
		snprintf(output_file, PATH_MAX - 1,"%s%s", (char *)opt->data,
			 eom_file_name);
	} else {
		sprintf(output_file, "%s", eom_file_name);
	}

	eom_result_count = (data->timing_max_steps * 2 + 1) *
			   (data->voltage_max_steps * 2 + 1) * data->num_lanes;
	eom_result_size = eom_result_count * sizeof(struct eom_result);
	data->er = calloc(1,eom_result_size);
	if (!data->er) {
		print_error("Failed to allocate memory for eom_result\n");
		goto out;
	}

	printf("EOM Capabilities:\n");
	printf("TimingMaxSteps %d TimingMaxOffset %d\n",
		data->timing_max_steps, data->timing_max_offset);
	printf("VoltageMaxSteps %d VoltageMaxOffset %d\n",
		data->voltage_max_steps, data->voltage_max_offset);

	printf("Start Eye Monitor...\n");
	clock_gettime(CLOCK_MONOTONIC, &ts_start);
	/* Main loop starts here */
	for (l = gen_id_selector - LANE0_SELECTOR, n = data->num_lanes; n > 0; n--, l++) {
		/* Sweep all timings */
		for (t = -data->timing_max_steps; t <= data->timing_max_steps; t++) {
			for (v = -data->voltage_max_steps; v <= data->voltage_max_steps; v++) {
				rc = eom_scan(fd, data->local_peer, l, t , v,
					      opt->test_count, fill_data_fd,
					      fill_buf);
				if (rc) {
					print_error("Fail to run EOM scan\n");
					goto out;
				}
			}
		}

		/* Disable Eye Monitor */
		rc = ufshcd_dme_set_attr(fd,
				UIC_ARG_MIB_SEL(RX_EYEMON_ENABLE, SELECT_RX(l)),
				ATTR_SET_NOR, 0, opt->idn);
		if (rc) {
			print_error("Failed to disable EOM for lane %d\n", l);
			goto out;
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &ts_end);
	printf("EOM Scan Finished!\n Time elapsed: %ld seconds\n", ts_end.tv_sec - ts_start.tv_sec);

	rc = generate_eom_report(fd, output_file, data);
	if (rc)
		print_error("Failed to generate EOM report\n");

out:
	if (fill_buf)
		free (fill_buf);

	if (fill_data_fd > 0)
		close(fill_data_fd);
	remove("fill_file");
	if (data->er)
		free(data->er);

	close(fd);
	return rc;
}

void ufs_emon_help(char *tool_name)
{
	printf("\n Eye monitor command usage:\n");
	printf("\n\t%s emon  [-t] <eye motinor side> [-x ] <time steps>\n"
		"\t\t[-y] <voltage steps> [-n] <test_count> [-p] <path to device>\n",
		tool_name);
	printf("\n\t-t\tEye Monitor Side: 0 local (host RX, 1 peer(device RX)\n");
	printf("\n\t-x\ttime steps [default: RX_EYEMON_TIMING_MAX_STEPS_CAPABILITY]\n");
	printf("\n\t-y\tvoltage steps [default: RX_EYEMON_VOLTAGE_MAX_STEPS_CAPABILITY]\n");
	printf("\n\t-n\tnumber of the test count [default: %d]\n", DEFAULT_TEST_COUNT);
	printf("\n\t-s\tgen selector [default: 0, get Eye for two lines]\n");
	printf("\n\t-p\tufs-bsg path\n");
}
