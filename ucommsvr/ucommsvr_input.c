/*
 * Micro Communicator for Projection uC
 * a High-Speed Serial communications server
 *
 * Input devices module
 *
 * Copyright (C) 2018 AngeloGioacchino Del Regno <kholk11@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#include <errno.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <linux/input.h>

#include <private/android_filesystem_config.h>
#include <utils/Log.h>

#include "ucomm_private.h"
#include "ucomm_input.h"
#include "ucomm_ext.h"

#define LOG_TAG			"MicroCommInput"

#define VL53L0_EVT		"/dev/input/event1"
#define VL53L0_STR		"STM VL53L0 proximity sensor"
#define VL53L0_HIGH_RANGE	"1"
#define VL53L0_HIGH_ACCURACY	"2"

#define ITERATE_MAX_DEVS	9

#define SYSFS_INPUT_STR		"/sys/class/input/input%d"
#define DEVFS_INPUT_STR		"/dev/input/event%d"

static const char inputdev_name[] = "STM VL53L0 proximity sensor";
static const char sysfs_input_str[] = "/sys/class/input/input";
static const char devfs_input_str[] = "/dev/input/event";
static int stmvl_fd;



enum {
	FD_TOF,
	FD_MAX
};

static bool ucithread_run[THREAD_MAX];
static pthread_t uci_pthreads[THREAD_MAX];
static int inotof_fd;
static struct inotify_tof *inotof;
static struct pollfd uci_pfds[FD_MAX];
static struct epoll_event uci_pollevt[FD_MAX];
static int uci_pollfd[FD_MAX];
static int uci_pfdelay_ms[FD_MAX];
static char *uci_tof_enable_path;
static bool tof_enabled = false;

struct micro_communicator_vl53l0 stmvl_status;

#define UNUSED __attribute__((unused))

#define LEN_NAME	4
#define LEN_ENAB	16
#define LEN_MODE	12

int ucomm_tof_enable(bool enable)
{
	int fd, rc;

	if (uci_tof_enable_path == NULL)
		return -1;

	fd = open(uci_tof_enable_path, O_WRONLY | O_SYNC);
	if (fd < 0) {
		ALOGD("Cannot open %s", uci_tof_enable_path);
		return 1;
	}

	if (enable)
		rc = write(fd, "1", 1);
	else
		rc = write(fd, "0", 1);

	if (rc < 1) {
		ALOGW("ERROR! Cannot %sable ToF!", enable ? "en" : "dis");
		goto end;
	}
	rc = 0;
end:
	fsync(fd);
	close(fd);
	usleep(100000);
	tof_enabled = enable;

	return rc;
}

static int ucomm_tof_sys_init(bool high_accuracy, int devno, int plen)
{
	char* sns_mode_path;
	int fd, rc;

	uci_tof_enable_path = (char*)calloc(plen + LEN_ENAB, sizeof(char));
	if (uci_tof_enable_path == NULL) {
		ALOGE("Memory exhausted. Cannot allocate.");
		return -3;
	}

	snprintf(uci_tof_enable_path, plen + LEN_ENAB,
			"%s%d/enable_ps_sensor", sysfs_input_str, devno);

	if (high_accuracy) {
		sns_mode_path = (char*)calloc(plen + LEN_MODE, sizeof(char));
		if (sns_mode_path == NULL) {
			ALOGE("Memory exhausted. Cannot allocate.");
			free(uci_tof_enable_path);
			return -3;
		}

		snprintf(sns_mode_path, plen + LEN_MODE,
			"%s%d/set_use_case", sysfs_input_str, devno);

		fd = open(sns_mode_path, O_WRONLY);
		if (fd < 0) {
			ALOGD("Cannot open %s", sns_mode_path);
			return 1;
		}

		rc = write(fd, VL53L0_HIGH_ACCURACY, 1);
		if (rc < 1)
			ALOGW("ERROR! Cannot set ToF High Accuracy mode!");

		close(fd);
	}

	return 0;
}

/* TODO: Use IOCTL EVIOCGNAME as a waaaay better way */
static int ucomm_find_inputdev(int maxdevs, int idev_len, char* idev_name)
{
	int fd, plen, rlen, vlen, rc, i;
	int plen_xtra = 3;
	bool found = false;
	char buf[254];
	char* path;

	/*
	 * Avoid checking more than 99 input devices,
	 * because... really? :)
	 */
	if (maxdevs > 99)
		return -1;

	if (maxdevs > 9)
		plen_xtra++;

	plen = strlen(sysfs_input_str) + plen_xtra;
	path = (char*) calloc(plen + LEN_NAME, sizeof(char));
	if (path == NULL) {
		ALOGE("Out of memory. Cannot allocate.");
		rc = -1;
		goto end;
	}

	for (i = 0; i <= maxdevs; i++) {
		rc = snprintf(path, plen + LEN_NAME, "%s%d/name",
							sysfs_input_str, i);

		if (fd > 0)
			close(fd);

		fd = open(path, (O_RDONLY | O_NONBLOCK));
		if (fd < 0) {
			ALOGD("Cannot open %s", path);
			continue;
		}

		rlen = read(fd, buf, idev_len);
		if (!rlen)
			continue;

		/*
		 * If the string length doesn't match, avoid doing
		 * expensive stuff, as the string cannot be the same.
		 * Also consider some mistakes and compare it with
		 * the name length - 1.
		 * That will cover corner cases and will not evaluate
		 * erroneously positive too many times...
		 */
		if (rlen < idev_len - 1)
			continue;

		rc = strncmp(buf, idev_name, idev_len - 1);
		if (rc == 0) {
			close(fd);

			rc = ucomm_tof_sys_init(VL53L0_HIGH_ACCURACY, i, plen);
			if (rc < 0)
				goto end;

			return i;
		}
	}
end:
	return rc;
}

/* TODO: Move me to background thread!! */
int ucomm_input_tof_read(struct micro_communicator_vl53l0 *stmvl_cur,
			uint16_t want_code)
{
	struct input_event evt[64];
	int retry = 0, i, len, rc;
	bool rd, rr, rs, out;
	uint16_t type, code;
	int32_t value;

	if (stmvl_fd < 0)
		return -1;

	stmvl_cur->range_status = 0;

again:
	rd = false;
	rr = false;
	rs = false;

	do {
		fsync(stmvl_fd);

		rc = read(stmvl_fd, &evt, sizeof(evt));
		if (!rc) {
			return rc;
		}

		len = rc / sizeof(struct input_event);

		for (i = 0; i < len; i++) {
			type = evt[i].type;
			code = evt[i].code;
			value = evt[i].value;

			if (type != EV_ABS)
				continue;

			switch (code)
			{
				case ABS_DISTANCE:
					if (value < 900 && value >= 0) {
						stmvl_cur->distance = value;
						rd = true;
					}
					break;
				case ABS_HAT1X:
					if (value < 9000 && value >= 0) {
						stmvl_cur->range_mm = value;
						rr = true;
					}
					break;
				case ABS_HAT1Y:
					stmvl_cur->range_status = value;
					rs = true;
					break;
				default:
					break;
			}

			if ((rr && rd && rs) || (want_code == code))
				goto done;
		}
	} while (!rd || !rr || !rs);

done:
	if (stmvl_cur->range_status != 0) {
		if (retry < 4) {
			retry++;
			goto again;
		} else {
			return -1;
		}
	}

	return 0;
}

int ucomm_input_tof_thr_read(struct micro_communicator_vl53l0 *stmvl_cur,
			int tof_fd)
{
	struct input_event evt[16];
	int retry = 0, i, len, rc;
	bool rd, rr, rs, out;
	uint16_t type, code;
	int32_t value;

	stmvl_cur->range_status = 0;

again:
	rd = false;
	rr = false;
	rs = false;

	rc = read(tof_fd, &evt, sizeof(evt));
	if (!rc) {
		return rc;
	}

	len = rc / sizeof(struct input_event);

	for (i = 0; i < len; i++) {
		type = evt[i].type;
		code = evt[i].code;
		value = evt[i].value;

		if (type != EV_ABS)
			continue;

		switch (code) {
			case ABS_DISTANCE:
				if (value < 900 && value >= 0) {
					stmvl_cur->distance = value;
					rd = true;
				}
				break;
			case ABS_HAT1X:
				if (value < 9000 && value >= 0) {
					stmvl_cur->range_mm = value;
					rr = true;
				}
				break;
			case ABS_HAT1Y:
				stmvl_cur->range_status = value;
				rs = true;
				break;
			default:
				break;
		}
	}

	return 0;
}

static inline bool ucomm_tof_is_val_ok(int d1, int d2, int hysteresis)
{
	int max = d2 + hysteresis;
	int min = d2 - hysteresis;

	if (d1 < max && d1 > min)
		return true;

	return false;
}

/*
 * ucomm_tof_thr_read_stabilized - Reads the ToF parameters and tries to
 *			       give back a value only if it is stable.
 *
 * \param stmvl_final - Final structure with ToF values
 * \param runs - Maximum number of times to read the ToF
 * \param nmatch - Number of times to match readings
 * \param sleep_ms - Delay between each read
 * \param hyst - Hysteresis, relative to the distance measurements
 *
 * \return Returns reliability of the measurement or -INT_MAX for error;
 */
int ucomm_tof_thr_read_stabilized(
	struct micro_communicator_vl53l0 *stmvl_final,
	int runs, int nmatch, int sleep_ms, int hyst)
{
	int rc, retry = 0, cur_dst, range, score, i;

	/* Thread not running, we'd read nothing good here! */
	if (!ucithread_run[THREAD_TOF])
		return -1;

	/* Sensor is disabled, what are we trying to read?! */
	if (!tof_enabled)
		return -1;

	/* Did we get called by someone who didn't read the docs? */
	if (runs < nmatch)
		runs = nmatch + 1;

again:
	score = 0;
	cur_dst = stmvl_status.distance;
	range = stmvl_status.range_mm;

	for (i = 0; i < runs; i++) {
		usleep(sleep_ms*1000);

		if (ucomm_tof_is_val_ok(range, stmvl_status.range_mm, hyst))
			score++;
		else
			score--;
	}

	/* Readings are very unstable! */
	if (score < 0 && retry < 4) {
		retry++;
		goto again;
	}

	stmvl_final->distance = cur_dst;
	stmvl_final->range_mm = range;

	return score;
}

/*
 * ucomm_tof_read_stabilized - Reads the ToF parameters and tries to
 *			       give back a value only if it is stable.
 *
 * \param stmvl_final - Final structure with ToF values
 * \param runs - Maximum number of times to read the ToF
 * \param nmatch - Number of times to match readings
 * \param sleep_ms - Delay between each read
 * \param hyst - Hysteresis, relative to the distance measurements
 *
 * \return Returns reliability of the measurement or -INT_MAX for error;
 */
int ucomm_tof_read_stabilized(
	struct micro_communicator_vl53l0 *stmvl_final,
	int runs, int nmatch, int sleep_ms, int hyst)
{
	struct micro_communicator_vl53l0 stmvl_cur;
	int rc, retry = 0, cur_dst, range, score = 0, i;

	/* Did we get called by someone who didn't read the docs? */
	if (runs < nmatch)
		runs = nmatch + 1;

again:
	rc = ucomm_input_tof_read(&stmvl_cur, ABS_HAT1X);
	if (rc < 0) {
		ALOGE("ToF read failed");
		//return -INT_MAX;
	}

	cur_dst = stmvl_cur.distance;
	range = stmvl_cur.range_mm;

	/* First run done before looping! */
	for (i = 0; i < runs; i++) {
		usleep(sleep_ms*1000);
		rc = ucomm_input_tof_read(&stmvl_cur, ABS_HAT1X);
		if (rc < 0)
			continue;
		if (ucomm_tof_is_val_ok(cur_dst, stmvl_cur.distance, hyst))
			score++;
		else
			score--;
	}

	/* Readings are very unstable! */
	if (score < 0 && retry < 4) {
		retry++;
		goto again;
	}

	stmvl_final->distance = cur_dst;
	stmvl_final->range_mm = range;

	return score;
}

static void *ucomm_input_tof_thread(void *unusedvar UNUSED)
{
	int ret;
	int i;
	struct epoll_event pevt[10];

	ucomm_tof_enable(true);

	ALOGD("ToF Thread started");

	while (ucithread_run[THREAD_TOF]) {
		ret = epoll_wait(uci_pollfd[FD_TOF], pevt,
					10, uci_pfdelay_ms[FD_TOF]);
		for (i = 0; i < ret; i++) {
			if (pevt[i].events & EPOLLERR ||
			    pevt[i].events & EPOLLHUP ||
			    !(pevt[i].events & EPOLLIN))
				continue;

			if (uci_pollevt[FD_TOF].data.fd)
				ucomm_input_tof_thr_read(&stmvl_status,
					uci_pollevt[FD_TOF].data.fd);
		}
	}

	ucomm_tof_enable(false);

	pthread_exit((void*)((int)0));
}

/* Start/stop threads */
int ucomm_input_threadman(bool start, int threadno)
{
	int ret = -1;

	if (start == false) {
		ucithread_run[threadno] = false;
		return 0;
	};

	ucithread_run[threadno] = true;

	if (threadno == THREAD_TOF) {
		ret = pthread_create(&uci_pthreads[threadno], NULL,
				ucomm_input_tof_thread, NULL);
		if (ret != 0) {
			ALOGE("Cannot create ToF thread.");
			return -ENXIO;
		}
	}

	return ret;
}

int ucomm_input_tof_init(void)
{
	int dlen, evtno, rc;
	char *devname, *devpath;

	dlen = strlen(VL53L0_STR);
	devname = (char*) calloc(dlen, sizeof(char));
	snprintf(devname, dlen, "%s", VL53L0_STR);

	evtno = ucomm_find_inputdev(ITERATE_MAX_DEVS, dlen, devname);
	if (evtno < 0)
		return -1;

	dlen = strlen(devfs_input_str) + 3;
	if (evtno > 9)
		dlen++;

	devpath = (char*) calloc(dlen, sizeof(char));
	if (devpath == NULL)
		return -1;

	snprintf(devpath, dlen, "%s%d", devfs_input_str, evtno);


	stmvl_fd = open(devpath, (O_RDONLY | O_NONBLOCK));
	if (stmvl_fd < 0) {
		ALOGE("Error: cannot open the %s input device at %s.",
			devname, devpath);
		return -1;
	}

	uci_pollfd[FD_TOF] = epoll_create1(0);
	if (uci_pollfd[FD_TOF] == -1) {
		ALOGE("Error: Cannot create epoll descriptor");
		return -1;
	}

	uci_pfds[FD_TOF].fd = stmvl_fd;
	uci_pfds[FD_TOF].events = POLLIN;
	uci_pfdelay_ms[FD_TOF] = 1000;

	uci_pollevt[FD_TOF].events = POLLIN; // | EPOLLET;
	uci_pollevt[FD_TOF].data.fd = stmvl_fd;

	rc = epoll_ctl(uci_pollfd[FD_TOF], EPOLL_CTL_ADD,
					stmvl_fd, &uci_pollevt[FD_TOF]);
	if (rc) {
		ALOGE("Cannot add epoll control");
		return -1;
	}

	ucithread_run[THREAD_TOF] = false;

	rc = ucomm_input_threadman(true, THREAD_TOF);
	if (rc < 0)
		return rc;

	return 0;
}

