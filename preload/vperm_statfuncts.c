/*
 * Copyright (C) 2011 Nokia Corporation.
 * Author: Lauri T. Aarnio
 *
 * Licensed under LGPL version 2.1, see top level LICENSE file for details.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "lb.h"
#include "lb_stat.h"
#include "lb_vperm.h"

#include "rule_tree.h"
#include "rule_tree_rpc.h"
#include "liblb.h"
#include "exported.h"


int real_lstat(const char *path, struct stat *statbuf)
{
	int	r;

#ifdef _STAT_VER
	r = __lxstat_nomap_nolog(_STAT_VER, path, statbuf);
#else
	r = lstat_nomap_nolog(path, statbuf);
#endif
	return(r);
}

int real_stat(const char *path, struct stat *statbuf)
{
	int	r;

#ifdef _STAT_VER
	r = __xstat_nomap_nolog(_STAT_VER, path, statbuf);
#else
	r = stat_nomap_nolog(path, statbuf);
#endif
	return(r);
}

int real_fstatat(int dirfd, const char *path, struct stat *statbuf, int flags)
{
	int	r;

#ifdef _STAT_VER
	r = __fxstatat_nomap_nolog(_STAT_VER, dirfd, path, statbuf, flags);
#else
	r = fstatat_nomap_nolog(dirfd, path, statbuf, flags);
#endif
	return(r);
}

int real_fstat(int fd, struct stat *statbuf)
{
	int	r;

#ifdef _STAT_VER
	r = __fxstat_nomap_nolog(_STAT_VER, fd, statbuf);
#else
	r = fstat_nomap_nolog(_STAT_VER, fd, statbuf);
#endif
	return(r);
}

/* return 0 if not modified, positive if something was virtualized.
 * only one of {buf,buf64} should be set; set the other one to NULL */
int i_virtualize_struct_stat(
	const char *realfnname,
	struct stat *buf,
	struct stat64 *buf64)
{
	int				res = 0;
	inodesimu_t      		istat_in_db;
	uid_t				uf_uid;
	gid_t				uf_gid;

	if (ruletree_rpc__get_inodestat(buf ? buf->st_dev : buf64->st_dev,
	                                buf ? buf->st_ino : buf64->st_ino,
	                                &istat_in_db) > 0) {
		int set_uid_gid_of_unknown = vperm_set_owner_and_group_of_unknown_files(
			&uf_uid, &uf_gid);

		LB_LOG(LB_LOGLEVEL_DEBUG, "%s/%s: inodestats struct found:", realfnname, __func__);
		if (istat_in_db.inodesimu_active_fields &
			RULETREE_INODESTAT_SIM_UID) {
			LB_LOG(LB_LOGLEVEL_DEBUG,
				"%s/%s: found, set uid to %d",
				realfnname, __func__, istat_in_db.inodesimu_uid);
			if (buf) buf->st_uid = istat_in_db.inodesimu_uid;
			else   buf64->st_uid = istat_in_db.inodesimu_uid;
			res++;
		} else if (set_uid_gid_of_unknown) {
			LB_LOG(LB_LOGLEVEL_DEBUG,
				"%s/%s: 'unknown' file, set owner to %d",
					realfnname, __func__, uf_uid);
			if (buf) buf->st_uid = uf_uid;
			else   buf64->st_uid = uf_uid;
			res++;
		}

		if (istat_in_db.inodesimu_active_fields &
			RULETREE_INODESTAT_SIM_GID) {
			LB_LOG(LB_LOGLEVEL_DEBUG,
				"%s/%s: found, set gid to %d",
				realfnname, __func__, istat_in_db.inodesimu_gid);
			if (buf) buf->st_gid = istat_in_db.inodesimu_gid;
			else   buf64->st_gid = istat_in_db.inodesimu_gid;
			res++;
		} else if (set_uid_gid_of_unknown) {
			LB_LOG(LB_LOGLEVEL_DEBUG,
				"%s/%s: 'unknown' file, set group to %d",
					realfnname, __func__, uf_gid);
			if (buf) buf->st_gid = uf_gid;
			else   buf64->st_gid = uf_gid;
			res++;
		}

		if (istat_in_db.inodesimu_active_fields &
			RULETREE_INODESTAT_SIM_MODE) {
			LB_LOG(LB_LOGLEVEL_DEBUG,
				"%s/%s: found, set mode to 0%o",
				realfnname, __func__, istat_in_db.inodesimu_mode);
			if (buf) buf->st_mode =
					(buf->st_mode & S_IFMT) |
					(istat_in_db.inodesimu_mode & (~S_IFMT));
			else   buf64->st_mode =
					(buf64->st_mode & S_IFMT) |
					(istat_in_db.inodesimu_mode & (~S_IFMT));
			res++;
		}
		if (istat_in_db.inodesimu_active_fields &
			RULETREE_INODESTAT_SIM_SUIDSGID) {
			LB_LOG(LB_LOGLEVEL_DEBUG,
				"%s/%s: found, set suid/sgid 0%o",
				realfnname, __func__, istat_in_db.inodesimu_suidsgid);
			if (buf) buf->st_mode =
				(buf->st_mode & ~(S_ISUID | S_ISGID)) |
				(istat_in_db.inodesimu_suidsgid & (S_ISUID | S_ISGID));
			else   buf64->st_mode =
				(buf64->st_mode & ~(S_ISUID | S_ISGID)) |
				(istat_in_db.inodesimu_suidsgid & (S_ISUID | S_ISGID));
			res++;
		}
		if (istat_in_db.inodesimu_active_fields &
			RULETREE_INODESTAT_SIM_DEVNODE) {
			LB_LOG(LB_LOGLEVEL_DEBUG,
				"%s/%s: found, simulated device 0%o,0x%X",
				realfnname, __func__, istat_in_db.inodesimu_devmode,
				(int)istat_in_db.inodesimu_rdev);
			if (buf) {
				buf->st_mode =
					(buf->st_mode & (~S_IFMT)) |
					(istat_in_db.inodesimu_devmode & S_IFMT);
				buf->st_rdev = istat_in_db.inodesimu_rdev;
			} else {
				buf64->st_mode =
					(buf64->st_mode & (~S_IFMT)) |
					(istat_in_db.inodesimu_devmode & S_IFMT);
				buf64->st_rdev = istat_in_db.inodesimu_rdev;
			}
			res++;
		}
	} else if (vperm_set_owner_and_group_of_unknown_files(&uf_uid, &uf_gid)) {
		LB_LOG(LB_LOGLEVEL_DEBUG,
			"%s/%s: 'unknown' file, set owner and group to %d.%d",
				realfnname, __func__, uf_uid, uf_gid);
		if (buf) buf->st_uid = uf_uid;
		else   buf64->st_uid = uf_uid;
		if (buf) buf->st_gid = uf_gid;
		else   buf64->st_gid = uf_gid;
		res += 2;
	}

	LB_LOG(LB_LOGLEVEL_DEBUG, "%s/%s: done, result=%d", realfnname, __func__, res);
	return(res);
}

int lb_stat_file(const char *path, struct stat *buf, int *result_errno_ptr,
	int (*statfn_with_ver_ptr)(int ver, const char *filename, struct stat *buf),
	int ver,
	int (*statfn_ptr)(const char *filename, struct stat *buf))
{
	int		res;

	LB_LOG(LB_LOGLEVEL_NOISE, "lb_stat(%s)", path);
	if (statfn_with_ver_ptr) {
		/* glibc; it doesn't have a real stat() function */
		res = (*statfn_with_ver_ptr)(ver, path, buf);
	} else if (statfn_ptr) {
		/* probably something else than glibc.
		 * hope this works (not tested...) */
		res = (*statfn_ptr)(path, buf);
	} else {
		*result_errno_ptr = EINVAL;
		return(-1);
	}

	if (res == 0) {
		i_virtualize_struct_stat(__func__, buf, NULL);
	} else {
		*result_errno_ptr = errno;
		LB_LOG(LB_LOGLEVEL_NOISE, "lb_stat(%s) : failed, errno=%d",
			path, errno);
	}
	LB_LOG(LB_LOGLEVEL_NOISE, "lb_stat(%s) returns %d", path, res);
	return(res);
}

int lb_stat64_file(const char *path, struct stat64 *buf, int *result_errno_ptr,
	int (*stat64fn_with_ver_ptr)(int ver, const char *filename, struct stat64 *buf),
	int ver,
	int (*stat64fn_ptr)(const char *filename, struct stat64 *buf))
{
	int		res;

	LB_LOG(LB_LOGLEVEL_NOISE, "lb_stat64(%s)", path);
	if (stat64fn_with_ver_ptr) {
		/* glibc; it doesn't have a real stat64() function */
		res = (*stat64fn_with_ver_ptr)(ver, path, buf);
	} else if (stat64fn_ptr) {
		/* probably something else than glibc.
		 * hope this works (not tested...) */
		res = (*stat64fn_ptr)(path, buf);
	} else {
		*result_errno_ptr = EINVAL;
		return(-1);
	}

	if (res == 0) {
		i_virtualize_struct_stat(__func__, NULL, buf);
	} else {
		*result_errno_ptr = errno;
		LB_LOG(LB_LOGLEVEL_NOISE, "lb_stat64(%s) : failed, errno=%d",
			path, errno);
	}
	LB_LOG(LB_LOGLEVEL_NOISE, "lb_stat64(%s) returns %d", path, res);
	return(res);
}

int lb_fstat(int fd, struct stat *statbuf)
{
	LB_LOG(LB_LOGLEVEL_NOISE, "lb_fstat(%d)", fd);
	if (real_fstat(fd, statbuf) < 0) return(-1);

	i_virtualize_struct_stat(__func__, statbuf, NULL);
	return(0);
}

