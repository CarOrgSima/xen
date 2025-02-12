/*
 * Copyright (C) 2009      Citrix Ltd.
 * Author Stefano Stabellini <stefano.stabellini@eu.citrix.com>
 * Author Vincent Hanquez <vincent.hanquez@eu.citrix.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; version 2.1 only. with the special
 * exception on linking described in file LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 */

#include "libxl_osdeps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h> /* for time */
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <xenctrl.h>
#include <ctype.h>

#include "libxl.h"
#include "libxl_utils.h"
#include "libxlutil.h"

#define UUID_FMT "%02hhx%02hhx%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx"

int logfile = 2;

void log_callback(void *userdata, int loglevel, const char *file, int line, const char *func, char *s)
{
    char str[1024];

    snprintf(str, sizeof(str), "[%d] %s:%d:%s: %s\n", loglevel, file, line, func, s);
    write(logfile, str, strlen(str));
}

static int domain_qualifier_to_domid(struct libxl_ctx *ctx, char *p, uint32_t *domid)
{
    int i, alldigit;

    alldigit = 1;
    for (i = 0; p[i]; i++) {
        if (!isdigit((uint8_t)p[i])) {
            alldigit = 0;
            break;
        }
    }

    if (i > 0 && alldigit) {
        *domid = strtoul(p, NULL, 10);
        return 0;
    } else {
        /* check here if it's a uuid and do proper conversion */
    }
    return libxl_name_to_domid(ctx, p, domid);
}

#define LOG(_f, _a...)   dolog(__FILE__, __LINE__, __func__, _f "\n", ##_a)

void dolog(const char *file, int line, const char *func, char *fmt, ...)
{
    va_list ap;
    char *s;
    int rc;

    va_start(ap, fmt);
    rc = vasprintf(&s, fmt, ap);
    va_end(ap);
    if (rc >= 0)
        write(logfile, s, rc);
}

static void init_create_info(libxl_domain_create_info *c_info)
{
    memset(c_info, '\0', sizeof(*c_info));
    c_info->xsdata = NULL;
    c_info->platformdata = NULL;
    c_info->hvm = 1;
    c_info->ssidref = 0;
}

static void init_build_info(libxl_domain_build_info *b_info, libxl_domain_create_info *c_info)
{
    memset(b_info, '\0', sizeof(*b_info));
    b_info->timer_mode = -1;
    b_info->hpet = 1;
    b_info->vpt_align = -1;
    b_info->max_vcpus = 1;
    b_info->max_memkb = 32 * 1024;
    b_info->target_memkb = b_info->max_memkb;
    if (c_info->hvm) {
        b_info->shadow_memkb = libxl_get_required_shadow_memory(b_info->max_memkb, b_info->max_vcpus);
        b_info->video_memkb = 8 * 1024;
        b_info->kernel = "/usr/lib/xen/boot/hvmloader";
        b_info->hvm = 1;
        b_info->u.hvm.pae = 1;
        b_info->u.hvm.apic = 1;
        b_info->u.hvm.acpi = 1;
        b_info->u.hvm.nx = 1;
        b_info->u.hvm.viridian = 0;
    } else {
        b_info->u.pv.slack_memkb = 8 * 1024;
    }
}

static void init_dm_info(libxl_device_model_info *dm_info,
        libxl_domain_create_info *c_info, libxl_domain_build_info *b_info)
{
    int i;
    memset(dm_info, '\0', sizeof(*dm_info));

    for (i = 0; i < 16; i++) {
        dm_info->uuid[i] = rand();
    }

    dm_info->dom_name = c_info->name;
    dm_info->device_model = "/usr/lib/xen/bin/qemu-dm";
    dm_info->videoram = b_info->video_memkb / 1024;
    dm_info->apic = b_info->u.hvm.apic;

    dm_info->stdvga = 0;
    dm_info->vnc = 1;
    dm_info->vnclisten = "127.0.0.1";
    dm_info->vncdisplay = 0;
    dm_info->vncunused = 0;
    dm_info->keymap = NULL;
    dm_info->sdl = 0;
    dm_info->opengl = 0;
    dm_info->nographic = 0;
    dm_info->serial = NULL;
    dm_info->boot = "cda";
    dm_info->usb = 0;
    dm_info->usbdevice = NULL;
}

static void init_nic_info(libxl_device_nic *nic_info, int devnum)
{
    memset(nic_info, '\0', sizeof(*nic_info));

    nic_info->backend_domid = 0;
    nic_info->domid = 0;
    nic_info->devid = devnum;
    nic_info->mtu = 1492;
    nic_info->model = "e1000";
    nic_info->mac[0] = 0x00;
    nic_info->mac[1] = 0x16;
    nic_info->mac[2] = 0x3e;
    nic_info->mac[3] = 1 + (int) (0x7f * (rand() / (RAND_MAX + 1.0)));
    nic_info->mac[4] = 1 + (int) (0xff * (rand() / (RAND_MAX + 1.0)));
    nic_info->mac[5] = 1 + (int) (0xff * (rand() / (RAND_MAX + 1.0)));
    nic_info->ifname = NULL;
    nic_info->bridge = "xenbr0";
    nic_info->script = "/etc/xen/scripts/vif-bridge";
    nic_info->nictype = NICTYPE_IOEMU;
}

static void init_vfb_info(libxl_device_vfb *vfb, int dev_num)
{
    memset(vfb, 0x00, sizeof(libxl_device_vfb));
    vfb->devid = dev_num;
    vfb->vnc = 1;
    vfb->vnclisten = "127.0.0.1";
    vfb->vncdisplay = 0;
    vfb->vncunused = 1;
    vfb->keymap = NULL;
    vfb->sdl = 0;
    vfb->opengl = 0;
}

static void init_vkb_info(libxl_device_vkb *vkb, int dev_num)
{
    memset(vkb, 0x00, sizeof(libxl_device_vkb));
    vkb->devid = dev_num;
}

static void init_console_info(libxl_device_console *console, int dev_num, libxl_domain_build_state *state)
{
    memset(console, 0x00, sizeof(libxl_device_console));
    console->devid = dev_num;
    console->constype = CONSTYPE_XENCONSOLED;
    if (state)
        console->build_state = state;
}

static void printf_info(libxl_domain_create_info *c_info,
                        libxl_domain_build_info *b_info,
                        libxl_device_disk *disks,
                        int num_disks,
                        libxl_device_nic *vifs,
                        int num_vifs,
                        libxl_device_pci *pcidevs,
                        int num_pcidevs,
                        libxl_device_vfb *vfbs,
                        int num_vfbs,
                        libxl_device_vkb *vkb,
                        int num_vkbs,
                        libxl_device_model_info *dm_info)
{
    int i;
    printf("*** domain_create_info ***\n");
    printf("hvm: %d\n", c_info->hvm);
    printf("hap: %d\n", c_info->hap);
    printf("ssidref: %d\n", c_info->ssidref);
    printf("name: %s\n", c_info->name);
    printf("uuid: " UUID_FMT "\n",
           (c_info->uuid)[0], (c_info->uuid)[1], (c_info->uuid)[2], (c_info->uuid)[3],
           (c_info->uuid)[4], (c_info->uuid)[5], (c_info->uuid)[6], (c_info->uuid)[7],
           (c_info->uuid)[8], (c_info->uuid)[9], (c_info->uuid)[10], (c_info->uuid)[11],
           (c_info->uuid)[12], (c_info->uuid)[13], (c_info->uuid)[14], (c_info->uuid)[15]);
    if (c_info->xsdata)
        printf("xsdata: contains data\n");
    else
        printf("xsdata: (null)\n");
    if (c_info->platformdata)
        printf("platformdata: contains data\n");
    else
        printf("platformdata: (null)\n");


    printf("\n\n\n*** domain_build_info ***\n");
    printf("timer_mode: %d\n", b_info->timer_mode);
    printf("hpet: %d\n", b_info->hpet);
    printf("vpt_align: %d\n", b_info->vpt_align);
    printf("max_vcpus: %d\n", b_info->max_vcpus);
    printf("max_memkb: %d\n", b_info->max_memkb);
    printf("target_memkb: %d\n", b_info->target_memkb);
    printf("kernel: %s\n", b_info->kernel);
    printf("hvm: %d\n", b_info->hvm);

    if (c_info->hvm) {
        printf("video_memkb: %d\n", b_info->video_memkb);
        printf("shadow_memkb: %d\n", b_info->shadow_memkb);
        printf("    pae: %d\n", b_info->u.hvm.pae);
        printf("    apic: %d\n", b_info->u.hvm.apic);
        printf("    acpi: %d\n", b_info->u.hvm.acpi);
        printf("    nx: %d\n", b_info->u.hvm.nx);
        printf("    viridian: %d\n", b_info->u.hvm.viridian);
    } else {
        printf("cmdline: %s\n", b_info->u.pv.cmdline);
        printf("ramdisk: %s\n", b_info->u.pv.ramdisk);
    }

    for (i = 0; i < num_disks; i++) {
        printf("\n\n\n*** disks_info: %d ***\n", i);
        printf("backend_domid %d\n", disks[i].backend_domid);
        printf("domid %d\n", disks[i].domid);
        printf("physpath %s\n", disks[i].physpath);
        printf("phystype %d\n", disks[i].phystype);
        printf("virtpath %s\n", disks[i].virtpath);
        printf("unpluggable %d\n", disks[i].unpluggable);
        printf("readwrite %d\n", disks[i].readwrite);
        printf("is_cdrom %d\n", disks[i].is_cdrom);
    }

    for (i = 0; i < num_vifs; i++) {
        printf("\n\n\n*** vifs_info: %d ***\n", i);
        printf("backend_domid %d\n", vifs[i].backend_domid);
        printf("domid %d\n", vifs[i].domid);
        printf("devid %d\n", vifs[i].devid);
        printf("mtu %d\n", vifs[i].mtu);
        printf("model %s\n", vifs[i].model);
        printf("mac %02x:%02x:%02x:%02x:%02x:%02x\n", vifs[i].mac[0], vifs[i].mac[1], vifs[i].mac[2], vifs[i].mac[3], vifs[i].mac[4], vifs[i].mac[5]);
    }

    for (i = 0; i < num_pcidevs; i++) {
        printf("\n\n\n*** pcidevs_info: %d ***\n", i);
        printf("pci dev "PCI_BDF_VDEVFN"\n", pcidevs[i].domain, pcidevs[i].bus, pcidevs[i].dev, pcidevs[i].func, pcidevs[i].vdevfn);
        printf("opts msitranslate %d power_mgmt %d\n", pcidevs[i].msitranslate, pcidevs[i].power_mgmt);
    }

    for (i = 0; i < num_vfbs; i++) {
        printf("\n\n\n*** vfbs_info: %d ***\n", i);
        printf("backend_domid %d\n", vfbs[i].backend_domid);
        printf("domid %d\n", vfbs[i].domid);
        printf("devid %d\n", vfbs[i].devid);
        printf("vnc: %d\n", vfbs[i].vnc);
        printf("vnclisten: %s\n", vfbs[i].vnclisten);
        printf("vncdisplay: %d\n", vfbs[i].vncdisplay);
        printf("vncunused: %d\n", vfbs[i].vncunused);
        printf("keymap: %s\n", vfbs[i].keymap);
        printf("sdl: %d\n", vfbs[i].sdl);
        printf("opengl: %d\n", vfbs[i].opengl);
        printf("display: %s\n", vfbs[i].display);
        printf("xauthority: %s\n", vfbs[i].xauthority);
    }

    if (c_info->hvm) {
        printf("\n\n\n*** device_model_info ***\n");
        printf("domid: %d\n", dm_info->domid);
        printf("dom_name: %s\n", dm_info->dom_name);
        printf("device_model: %s\n", dm_info->device_model);
        printf("videoram: %d\n", dm_info->videoram);
        printf("stdvga: %d\n", dm_info->stdvga);
        printf("vnc: %d\n", dm_info->vnc);
        printf("vnclisten: %s\n", dm_info->vnclisten);
        printf("vncdisplay: %d\n", dm_info->vncdisplay);
        printf("vncunused: %d\n", dm_info->vncunused);
        printf("keymap: %s\n", dm_info->keymap);
        printf("sdl: %d\n", dm_info->sdl);
        printf("opengl: %d\n", dm_info->opengl);
        printf("nographic: %d\n", dm_info->nographic);
        printf("serial: %s\n", dm_info->serial);
        printf("boot: %s\n", dm_info->boot);
        printf("usb: %d\n", dm_info->usb);
        printf("usbdevice: %s\n", dm_info->usbdevice);
        printf("apic: %d\n", dm_info->apic);
    }
}

static void parse_config_file(const char *filename,
                              libxl_domain_create_info *c_info,
                              libxl_domain_build_info *b_info,
                              libxl_device_disk **disks,
                              int *num_disks,
                              libxl_device_nic **vifs,
                              int *num_vifs,
                              libxl_device_pci **pcidevs,
                              int *num_pcidevs,
                              libxl_device_vfb **vfbs,
                              int *num_vfbs,
                              libxl_device_vkb **vkbs,
                              int *num_vkbs,
                              libxl_device_model_info *dm_info)
{
    const char *buf;
    long l;
    XLU_Config *config;
    XLU_ConfigList *vbds, *nics, *pcis, *cvfbs;
    int pci_power_mgmt = 0;
    int pci_msitranslate = 1;
    int i, e;

    config= xlu_cfg_init(stderr, filename);
    if (!config) {
        fprintf(stderr, "Failed to allocate for configuration\n");
        exit(1);
    }

    e= xlu_cfg_readfile (config, filename);
    if (e) {
        fprintf(stderr, "Failed to parse config file: %s\n", strerror(e));
        exit(1);
    }

    init_create_info(c_info);

    c_info->hvm = 0;
    if (!xlu_cfg_get_string (config, "builder", &buf) &&
        !strncmp(buf, "hvm", strlen(buf)))
        c_info->hvm = 1;

    /* hap is missing */
    if (!xlu_cfg_get_string (config, "name", &buf))
        c_info->name = strdup(buf);
    else
        c_info->name = "test";
    for (i = 0; i < 16; i++) {
        c_info->uuid[i] = rand();
    }

    init_build_info(b_info, c_info);

    /* the following is the actual config parsing with overriding values in the structures */
    if (!xlu_cfg_get_long (config, "vcpus", &l))
        b_info->max_vcpus = l;

    if (!xlu_cfg_get_long (config, "memory", &l)) {
        b_info->max_memkb = l * 1024;
        b_info->target_memkb = b_info->max_memkb;
    }

    if (!xlu_cfg_get_long (config, "shadow_memory", &l))
        b_info->shadow_memkb = l * 1024;

    if (!xlu_cfg_get_long (config, "videoram", &l))
        b_info->video_memkb = l * 1024;

    if (!xlu_cfg_get_string (config, "kernel", &buf))
        b_info->kernel = strdup(buf);

    if (c_info->hvm == 1) {
        if (!xlu_cfg_get_long (config, "pae", &l))
            b_info->u.hvm.pae = l;
        if (!xlu_cfg_get_long (config, "apic", &l))
            b_info->u.hvm.apic = l;
        if (!xlu_cfg_get_long (config, "acpi", &l))
            b_info->u.hvm.acpi = l;
        if (!xlu_cfg_get_long (config, "nx", &l))
            b_info->u.hvm.nx = l;
        if (!xlu_cfg_get_long (config, "viridian", &l))
            b_info->u.hvm.viridian = l;
    } else {
        char *cmdline;
        if (!xlu_cfg_get_string (config, "root", &buf)) {
            asprintf(&cmdline, "root=%s", buf);
            b_info->u.pv.cmdline = cmdline;
        }
        if (!xlu_cfg_get_string (config, "ramdisk", &buf))
            b_info->u.pv.ramdisk = strdup(buf);
    }

    if (!xlu_cfg_get_list (config, "disk", &vbds, 0)) {
        *num_disks = 0;
        *disks = NULL;
        while ((buf = xlu_cfg_get_listitem (vbds, *num_disks)) != NULL) {
            char *buf2 = strdup(buf);
            char *p, *p2;
            *disks = (libxl_device_disk *) realloc(*disks, sizeof (libxl_device_disk) * ((*num_disks) + 1));
            (*disks)[*num_disks].backend_domid = 0;
            (*disks)[*num_disks].domid = 0;
            (*disks)[*num_disks].unpluggable = 0;
            p = strtok(buf2, ",:");
            while (*p == ' ')
                p++;
            if (!strcmp(p, "phy")) {
                (*disks)[*num_disks].phystype = PHYSTYPE_PHY;
            } else if (!strcmp(p, "file")) {
                (*disks)[*num_disks].phystype = PHYSTYPE_FILE;
            } else if (!strcmp(p, "tap")) {
                p = strtok(NULL, ":");
                if (!strcmp(p, "aio")) {
                    (*disks)[*num_disks].phystype = PHYSTYPE_AIO;
                } else if (!strcmp(p, "vhd")) {
                    (*disks)[*num_disks].phystype = PHYSTYPE_VHD;
                } else if (!strcmp(p, "qcow")) {
                    (*disks)[*num_disks].phystype = PHYSTYPE_QCOW;
                } else if (!strcmp(p, "qcow2")) {
                    (*disks)[*num_disks].phystype = PHYSTYPE_QCOW2;
                }
            }
            p = strtok(NULL, ",");
            while (*p == ' ')
                p++;
            (*disks)[*num_disks].physpath= strdup(p);
            p = strtok(NULL, ",");
            while (*p == ' ')
                p++;
            p2 = strchr(p, ':');
            if (p2 == NULL) {
                (*disks)[*num_disks].virtpath = strdup(p);
                (*disks)[*num_disks].is_cdrom = 0;
                (*disks)[*num_disks].unpluggable = 1;
            } else {
                *p2 = '\0';
                (*disks)[*num_disks].virtpath = strdup(p);
                if (!strcmp(p2 + 1, "cdrom")) {
                    (*disks)[*num_disks].is_cdrom = 1;
                    (*disks)[*num_disks].unpluggable = 1;
                } else
                    (*disks)[*num_disks].is_cdrom = 0;
            }
            p = strtok(NULL, ",");
            while (*p == ' ')
                p++;
            (*disks)[*num_disks].readwrite = (p[0] == 'w') ? 1 : 0;
            free(buf2);
            *num_disks = (*num_disks) + 1;
        }
    }

    if (!xlu_cfg_get_list (config, "vif", &nics, 0)) {
        *num_vifs = 0;
        *vifs = NULL;
        while ((buf = xlu_cfg_get_listitem (nics, *num_vifs)) != NULL) {
            char *buf2 = strdup(buf);
            char *p, *p2;
            *vifs = (libxl_device_nic *) realloc(*vifs, sizeof (libxl_device_nic) * ((*num_vifs) + 1));
            init_nic_info((*vifs) + (*num_vifs), (*num_vifs) + 1);
            p = strtok(buf2, ",");
            if (!p)
                goto skip;
            do {
                while (*p == ' ')
                    p++;
                if ((p2 = strchr(p, '=')) == NULL)
                    break;
                *p2 = '\0';
                if (!strcmp(p, "model")) {
                    (*vifs)[*num_vifs].model = strdup(p2 + 1);
                } else if (!strcmp(p, "mac")) {
                    char *p3 = p2 + 1;
                    *(p3 + 2) = '\0';
                    (*vifs)[*num_vifs].mac[0] = strtol(p3, NULL, 16);
                    p3 = p3 + 3;
                    *(p3 + 2) = '\0';
                    (*vifs)[*num_vifs].mac[1] = strtol(p3, NULL, 16);
                    p3 = p3 + 3;
                    *(p3 + 2) = '\0';
                    (*vifs)[*num_vifs].mac[2] = strtol(p3, NULL, 16);
                    p3 = p3 + 3;
                    *(p3 + 2) = '\0';
                    (*vifs)[*num_vifs].mac[3] = strtol(p3, NULL, 16);
                    p3 = p3 + 3;
                    *(p3 + 2) = '\0';
                    (*vifs)[*num_vifs].mac[4] = strtol(p3, NULL, 16);
                    p3 = p3 + 3;
                    *(p3 + 2) = '\0';
                    (*vifs)[*num_vifs].mac[5] = strtol(p3, NULL, 16);
                } else if (!strcmp(p, "bridge")) {
                    (*vifs)[*num_vifs].bridge = strdup(p2 + 1);
                } else if (!strcmp(p, "type")) {
                    if (!strcmp(p2 + 1, "ioemu"))
                        (*vifs)[*num_vifs].nictype = NICTYPE_IOEMU;
                    else
                        (*vifs)[*num_vifs].nictype = NICTYPE_VIF;
                } else if (!strcmp(p, "ip")) {
                    inet_pton(AF_INET, p2 + 1, &((*vifs)[*num_vifs].ip));
                } else if (!strcmp(p, "script")) {
                    (*vifs)[*num_vifs].script = strdup(p2 + 1);
                } else if (!strcmp(p, "vifname")) {
                    (*vifs)[*num_vifs].ifname = strdup(p2 + 1);
                } else if (!strcmp(p, "rate")) {
                    fprintf(stderr, "the rate parameter for vifs is currently not supported\n");
                } else if (!strcmp(p, "accel")) {
                    fprintf(stderr, "the accel parameter for vifs is currently not supported\n");
                }
            } while ((p = strtok(NULL, ",")) != NULL);
skip:
            free(buf2);
            *num_vifs = (*num_vifs) + 1;
        }
    }

    if (!xlu_cfg_get_list (config, "vfb", &cvfbs, 0)) {
        *num_vfbs = 0;
        *num_vkbs = 0;
        *vfbs = NULL;
        *vkbs = NULL;
        while ((buf = xlu_cfg_get_listitem (cvfbs, *num_vfbs)) != NULL) {
            char *buf2 = strdup(buf);
            char *p, *p2;
            *vfbs = (libxl_device_vfb *) realloc(*vfbs, sizeof(libxl_device_vfb) * ((*num_vfbs) + 1));
            init_vfb_info((*vfbs) + (*num_vfbs), (*num_vfbs));

            *vkbs = (libxl_device_vkb *) realloc(*vkbs, sizeof(libxl_device_vkb) * ((*num_vkbs) + 1));
            init_vkb_info((*vkbs) + (*num_vkbs), (*num_vkbs));

            p = strtok(buf2, ",");
            if (!p)
                goto skip_vfb;
            do {
                while (*p == ' ')
                    p++;
                if ((p2 = strchr(p, '=')) == NULL)
                    break;
                *p2 = '\0';
                if (!strcmp(p, "vnc")) {
                    (*vfbs)[*num_vfbs].vnc = atoi(p2 + 1);
                } else if (!strcmp(p, "vnclisten")) {
                    (*vfbs)[*num_vfbs].vnclisten = strdup(p2 + 1);
                } else if (!strcmp(p, "vncdisplay")) {
                    (*vfbs)[*num_vfbs].vncdisplay = atoi(p2 + 1);
                } else if (!strcmp(p, "vncunused")) {
                    (*vfbs)[*num_vfbs].vncunused = atoi(p2 + 1);
                } else if (!strcmp(p, "keymap")) {
                    (*vfbs)[*num_vfbs].keymap = strdup(p2 + 1);
                } else if (!strcmp(p, "sdl")) {
                    (*vfbs)[*num_vfbs].sdl = atoi(p2 + 1);
                } else if (!strcmp(p, "opengl")) {
                    (*vfbs)[*num_vfbs].opengl = atoi(p2 + 1);
                } else if (!strcmp(p, "display")) {
                    (*vfbs)[*num_vfbs].display = strdup(p2 + 1);
                } else if (!strcmp(p, "xauthority")) {
                    (*vfbs)[*num_vfbs].xauthority = strdup(p2 + 1);
                }
            } while ((p = strtok(NULL, ",")) != NULL);
skip_vfb:
            free(buf2);
            *num_vfbs = (*num_vfbs) + 1;
            *num_vkbs = (*num_vkbs) + 1;
        }
    }

    if (!xlu_cfg_get_long (config, "pci_msitranslate", &l))
        pci_msitranslate = l;

    if (!xlu_cfg_get_long (config, "pci_power_mgmt", &l))
        pci_power_mgmt = l;

    if (!xlu_cfg_get_list (config, "pci", &pcis, 0)) {
        *num_pcidevs = 0;
        *pcidevs = NULL;
        while ((buf = xlu_cfg_get_listitem (pcis, *num_pcidevs)) != NULL) {
            unsigned int domain = 0, bus = 0, dev = 0, func = 0, vdevfn = 0;
            char *buf2 = strdup(buf);
            char *p;
            *pcidevs = (libxl_device_pci *) realloc(*pcidevs, sizeof (libxl_device_pci) * ((*num_pcidevs) + 1));
            memset(*pcidevs + *num_pcidevs, 0x00, sizeof(libxl_device_pci));
            p = strtok(buf2, ",");
            if (!p)
                goto skip_pci;
            if (!sscanf(p, PCI_BDF_VDEVFN, &domain, &bus, &dev, &func, &vdevfn)) {
                sscanf(p, "%02x:%02x.%01x@%02x", &bus, &dev, &func, &vdevfn);
                domain = 0;
            }
            libxl_device_pci_init(*pcidevs + *num_pcidevs, domain, bus, dev, func, vdevfn);
            (*pcidevs)[*num_pcidevs].msitranslate = pci_msitranslate;
            (*pcidevs)[*num_pcidevs].power_mgmt = pci_power_mgmt;
            while ((p = strtok(NULL, ",=")) != NULL) {
                while (*p == ' ')
                    p++;
                if (!strcmp(p, "msitranslate")) {
                    p = strtok(NULL, ",=");
                    (*pcidevs)[*num_pcidevs].msitranslate = atoi(p);
                } else if (!strcmp(p, "power_mgmt")) {
                    p = strtok(NULL, ",=");
                    (*pcidevs)[*num_pcidevs].power_mgmt = atoi(p);
                }
            }
            *num_pcidevs = (*num_pcidevs) + 1;
skip_pci:
            free(buf2);
        }
    }

    if (c_info->hvm == 1) {
        /* init dm from c and b */
        init_dm_info(dm_info, c_info, b_info);

        /* then process config related to dm */
        if (!xlu_cfg_get_string (config, "device_model", &buf))
            dm_info->device_model = strdup(buf);
        if (!xlu_cfg_get_long (config, "stdvga", &l))
            dm_info->stdvga = l;
        if (!xlu_cfg_get_long (config, "vnc", &l))
            dm_info->vnc = l;
        if (!xlu_cfg_get_string (config, "vnclisten", &buf))
            dm_info->vnclisten = strdup(buf);
        if (!xlu_cfg_get_long (config, "vncdisplay", &l))
            dm_info->vncdisplay = l;
        if (!xlu_cfg_get_long (config, "vncunused", &l))
            dm_info->vncunused = l;
        if (!xlu_cfg_get_string (config, "keymap", &buf))
            dm_info->keymap = strdup(buf);
        if (!xlu_cfg_get_long (config, "sdl", &l))
            dm_info->sdl = l;
        if (!xlu_cfg_get_long (config, "opengl", &l))
            dm_info->opengl = l;
        if (!xlu_cfg_get_long (config, "nographic", &l))
            dm_info->nographic = l;
        if (!xlu_cfg_get_string (config, "serial", &buf))
            dm_info->serial = strdup(buf);
        if (!xlu_cfg_get_string (config, "boot", &buf))
            dm_info->boot = strdup(buf);
        if (!xlu_cfg_get_long (config, "usb", &l))
            dm_info->usb = l;
        if (!xlu_cfg_get_string (config, "usbdevice", &buf))
            dm_info->usbdevice = strdup(buf);
    }

    dm_info->type = c_info->hvm ? XENFV : XENPV;

    xlu_cfg_destroy(config);
}

#define MUST( call ) ({                                                 \
        int must_rc = (call);                                           \
        if (must_rc) {                                                  \
            fprintf(stderr,"xl: fatal error: %s:%d, rc=%d: %s\n",       \
                    __FILE__,__LINE__, must_rc, #call);                 \
            exit(-must_rc);                                             \
        }                                                               \
    })

static void create_domain(int debug, int daemonize, const char *config_file, const char *restore_file, int paused)
{
    struct libxl_ctx ctx;
    uint32_t domid;
    libxl_domain_create_info info1;
    libxl_domain_build_info info2;
    libxl_domain_build_state state;
    libxl_device_model_info dm_info;
    libxl_device_disk *disks = NULL;
    libxl_device_nic *vifs = NULL;
    libxl_device_pci *pcidevs = NULL;
    libxl_device_vfb *vfbs = NULL;
    libxl_device_vkb *vkbs = NULL;
    libxl_device_console console;
    int num_disks = 0, num_vifs = 0, num_pcidevs = 0, num_vfbs = 0, num_vkbs = 0;
    int i, fd;
    int need_daemon = 1;
    int ret;
    libxl_device_model_starting *dm_starting = 0;
    libxl_waiter *w1 = NULL, *w2 = NULL;
    memset(&dm_info, 0x00, sizeof(dm_info));

    printf("Parsing config file %s\n", config_file);
    parse_config_file(config_file, &info1, &info2, &disks, &num_disks, &vifs, &num_vifs, &pcidevs, &num_pcidevs, &vfbs, &num_vfbs, &vkbs, &num_vkbs, &dm_info);
    if (debug)
        printf_info(&info1, &info2, disks, num_disks, vifs, num_vifs, pcidevs, num_pcidevs, vfbs, num_vfbs, vkbs, num_vkbs, &dm_info);

start:
    domid = 0;

    if (libxl_ctx_init(&ctx, LIBXL_VERSION)) {
        fprintf(stderr, "cannot init xl context\n");
        return;
    }

    libxl_ctx_set_log(&ctx, log_callback, NULL);

    ret = libxl_domain_make(&ctx, &info1, &domid);
    if (ret) {
        fprintf(stderr, "cannot make domain: %d\n", ret);
        return;
    }

    if (!restore_file || !need_daemon) {
        if (dm_info.saved_state) {
            free(dm_info.saved_state);
            dm_info.saved_state = NULL;
        }
        ret = libxl_domain_build(&ctx, &info2, domid, &state);
    } else {
        int restore_fd;

        restore_fd = open(restore_file, O_RDONLY);
        ret = libxl_domain_restore(&ctx, &info2, domid, restore_fd, &state, &dm_info);
        close(restore_fd);
    }

    if (ret) {
        fprintf(stderr, "cannot (re-)build domain: %d\n", ret);
        return;
    }

    for (i = 0; i < num_disks; i++) {
        disks[i].domid = domid;
        ret = libxl_device_disk_add(&ctx, domid, &disks[i]);
        if (ret) {
            fprintf(stderr, "cannot add disk %d to domain: %d\n", i, ret);
            return;
        }
    }
    for (i = 0; i < num_vifs; i++) {
        vifs[i].domid = domid;
        ret = libxl_device_nic_add(&ctx, domid, &vifs[i]);
        if (ret) {
            fprintf(stderr, "cannot add nic %d to domain: %d\n", i, ret);
            return;
        }
    }
    if (info1.hvm) {
        dm_info.domid = domid;
        MUST( libxl_create_device_model(&ctx, &dm_info, disks, num_disks,
                                        vifs, num_vifs, &dm_starting) );
    } else {
        for (i = 0; i < num_vfbs; i++) {
            vfbs[i].domid = domid;
            libxl_device_vfb_add(&ctx, domid, &vfbs[i]);
            vkbs[i].domid = domid;
            libxl_device_vkb_add(&ctx, domid, &vkbs[i]);
        }
        init_console_info(&console, 0, &state);
        console.domid = domid;
        if (num_vfbs)
            console.constype = CONSTYPE_IOEMU;
        libxl_device_console_add(&ctx, domid, &console);
        if (num_vfbs)
            libxl_create_xenpv_qemu(&ctx, vfbs, 1, &console, &dm_starting);
    }

    if (dm_starting)
        MUST( libxl_confirm_device_model_startup(&ctx, dm_starting) );
    for (i = 0; i < num_pcidevs; i++)
        libxl_device_pci_add(&ctx, domid, &pcidevs[i]);

    if (!paused)
        libxl_domain_unpause(&ctx, domid);

    if (!daemonize)
        exit(0);

    if (need_daemon) {
        char *fullname, *name;

        asprintf(&name, "xl-%s", info1.name);
        libxl_create_logfile(&ctx, name, &fullname);
        logfile = open(fullname, O_WRONLY|O_CREAT, 0644);
        free(fullname);
        free(name);

        daemon(0, 0);
        need_daemon = 0;
    }
    LOG("Waiting for domain %s (domid %d) to die", info1.name, domid);
    w1 = (libxl_waiter*) malloc(sizeof(libxl_waiter) * num_disks);
    w2 = (libxl_waiter*) malloc(sizeof(libxl_waiter));
    libxl_wait_for_disk_ejects(&ctx, domid, disks, num_disks, w1);
    libxl_wait_for_domain_death(&ctx, domid, w2);
    libxl_get_wait_fd(&ctx, &fd);
    while (1) {
        int ret;
        fd_set rfds;
        xc_domaininfo_t info;
        libxl_event event;
        libxl_device_disk disk;
        memset(&info, 0x00, sizeof(xc_dominfo_t));

        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        ret = select(fd + 1, &rfds, NULL, NULL, NULL);
        if (!ret)
            continue;
        libxl_get_event(&ctx, &event);
        switch (event.type) {
            case DOMAIN_DEATH:
                if (libxl_event_get_domain_death_info(&ctx, domid, &event, &info)) {
                    LOG("Domain %d is dead", domid);
                    if (info.flags & XEN_DOMINF_dying || (info.flags & XEN_DOMINF_shutdown && (((info.flags >> XEN_DOMINF_shutdownshift) & XEN_DOMINF_shutdownmask) != SHUTDOWN_suspend))) {
                        LOG("Domain %d needs to be clean: destroying the domain", domid);
                        libxl_domain_destroy(&ctx, domid, 0);
                        if (info.flags & XEN_DOMINF_shutdown &&
                            (((info.flags >> XEN_DOMINF_shutdownshift) & XEN_DOMINF_shutdownmask) == SHUTDOWN_reboot)) {
                            libxl_free_waiter(w1);
                            libxl_free_waiter(w2);
                            free(w1);
                            free(w2);
                            libxl_ctx_free(&ctx);
                            LOG("Done. Rebooting now");
                            goto start;
                        }
                        LOG("Done. Exiting now");
                    }
                    LOG("Domain %d does not need to be clean, exiting now", domid);
                    exit(0);
                }
                break;
            case DISK_EJECT:
                if (libxl_event_get_disk_eject_info(&ctx, domid, &event, &disk))
                    libxl_cdrom_insert(&ctx, domid, &disk);
                break;
        }
        libxl_free_event(&event);
    }

    close(logfile);
    free(disks);
    free(vifs);
    free(vfbs);
    free(vkbs);
    free(pcidevs);
}

static void help(char *command)
{
    if (!command || !strcmp(command, "help")) {
        printf("Usage xl <subcommand> [args]\n\n");
        printf("xl full list of subcommands:\n\n");
        printf(" create                        create a domain from config file <filename>\n\n");
        printf(" list                          list information about all domains\n\n");
        printf(" destroy                       terminate a domain immediately\n\n");
        printf(" pci-attach                    insert a new pass-through pci device\n\n");
        printf(" pci-detach                    remove a domain's pass-through pci device\n\n");
        printf(" pci-list                      list pass-through pci devices for a domain\n\n");
        printf(" pause                         pause execution of a domain\n\n");
        printf(" unpause                       unpause a paused domain\n\n");
        printf(" console                       attach to domain's console\n\n");
        printf(" save                          save a domain state to restore later\n\n");
        printf(" restore                       restore a domain from a saved state\n\n");
        printf(" cd-insert                     insert a cdrom into a guest's cd drive\n\n");
        printf(" cd-eject                      eject a cdrom from a guest's cd drive\n\n");
        printf(" mem-set                       set the current memory usage for a domain\n\n");
        printf(" button-press                  indicate an ACPI button press to the domain\n\n");
    } else if(!strcmp(command, "create")) {
        printf("Usage: xl create <ConfigFile> [options] [vars]\n\n");
        printf("Create a domain based on <ConfigFile>.\n\n");
        printf("Options:\n\n");
        printf("-h                     Print this help.\n");
        printf("-d                     Enable debug messages.\n");
        printf("-e                     Do not wait in the background for the death of the domain.\n");
    } else if(!strcmp(command, "list")) {
        printf("Usage: xl list [Domain]\n\n");
        printf("List information about all/some domains.\n\n");
    } else if(!strcmp(command, "pci-attach")) {
        printf("Usage: xl pci-attach <Domain> <BDF> [Virtual Slot]\n\n");
        printf("Insert a new pass-through pci device.\n\n");
    } else if(!strcmp(command, "pci-detach")) {
        printf("Usage: xl pci-detach <Domain> <BDF>\n\n");
        printf("Remove a domain's pass-through pci device.\n\n");
    } else if(!strcmp(command, "pci-list")) {
        printf("Usage: xl pci-list <Domain>\n\n");
        printf("List pass-through pci devices for a domain.\n\n");
    } else if(!strcmp(command, "pause")) {
        printf("Usage: xl pause <Domain>\n\n");
        printf("Pause execution of a domain.\n\n");
    } else if(!strcmp(command, "unpause")) {
        printf("Usage: xl unpause <Domain>\n\n");
        printf("Unpause a paused domain.\n\n");
    } else if(!strcmp(command, "save")) {
        printf("Usage: xl save [options] <Domain> <CheckpointFile>\n\n");
        printf("Save a domain state to restore later.\n\n");
        printf("Options:\n\n");
        printf("-h                     Print this help.\n");
        printf("-c                     Leave domain running after creating the snapshot.\n");
    } else if(!strcmp(command, "restore")) {
        printf("Usage: xl restore [options] <ConfigFile> <CheckpointFile>\n\n");
        printf("Restore a domain from a saved state.\n\n");
        printf("Options:\n\n");
        printf("-h                     Print this help.\n");
        printf("-p                     Do not unpause domain after restoring it.\n");
        printf("-e                     Do not wait in the background for the death of the domain.\n");
    } else if(!strcmp(command, "destroy")) {
        printf("Usage: xl destroy <Domain>\n\n");
        printf("Terminate a domain immediately.\n\n");
    } else if (!strcmp(command, "console")) {
        printf("Usage: xl console <Domain>\n\n");
        printf("Attach to domain's console.\n\n");
    } else if (!strcmp(command, "cd-insert")) {
        printf("Usage: xl cd-insert <Domain> <VirtualDevice> <type:path>\n\n");
        printf("Insert a cdrom into a guest's cd drive.\n\n");
    } else if (!strcmp(command, "cd-eject")) {
        printf("Usage: xl cd-eject <Domain> <VirtualDevice>\n\n");
        printf("Eject a cdrom from a guest's cd drive.\n\n");
    } else if (!strcmp(command, "mem-set")) {
        printf("Usage: xl mem-set <Domain> <MemKB>\n\n");
        printf("Set the current memory usage for a domain.\n\n");
    } else if (!strcmp(command, "button-press")) {
        printf("Usage: xl button-press <Domain> <Button>\n\n");
        printf("Indicate <Button> press to a domain.\n");
        printf("<Button> may be 'power' or 'sleep'.\n\n");
    }
}

void set_memory_target(char *p, char *mem)
{
    struct libxl_ctx ctx;
    uint32_t domid;
    uint32_t memorykb;
    char *endptr;

    if (libxl_ctx_init(&ctx, LIBXL_VERSION)) {
        fprintf(stderr, "cannot init xl context\n");
        return;
    }
    libxl_ctx_set_log(&ctx, log_callback, NULL);

    if (domain_qualifier_to_domid(&ctx, p, &domid) < 0) {
        fprintf(stderr, "%s is an invalid domain identifier\n", p);
        exit(2);
    }
    memorykb = strtoul(mem, &endptr, 10);
    if (*endptr != '\0') {
        fprintf(stderr, "invalid memory size: %s\n", mem);
        exit(3);
    }
    printf("setting domid %d memory to : %d\n", domid, memorykb);
    libxl_set_memory_target(&ctx, domid, memorykb);
}

int main_memset(int argc, char **argv)
{
    int opt = 0;
    char *p = NULL, *mem;

    while ((opt = getopt(argc, argv, "h:")) != -1) {
        switch (opt) {
        case 'h':
            help("mem-set");
            exit(0);
        default:
            fprintf(stderr, "option not supported\n");
            break;
        }
    }
    if (optind >= argc - 1) {
        help("mem-set");
        exit(2);
    }

    p = argv[optind];
    mem = argv[optind + 1];

    set_memory_target(p, mem);
    exit(0);
}

void console(char *p, int cons_num)
{
    struct libxl_ctx ctx;
    uint32_t domid;

    if (libxl_ctx_init(&ctx, LIBXL_VERSION)) {
        fprintf(stderr, "cannot init xl context\n");
        return;
    }
    libxl_ctx_set_log(&ctx, log_callback, NULL);

    if (domain_qualifier_to_domid(&ctx, p, &domid) < 0) {
        fprintf(stderr, "%s is an invalid domain identifier\n", p);
        exit(2);
    }
    libxl_console_attach(&ctx, domid, cons_num);
}

void cd_insert(char *dom, char *virtdev, char *phys)
{
    struct libxl_ctx ctx;
    uint32_t domid;
    libxl_device_disk disk;
    char *p;

    if (libxl_ctx_init(&ctx, LIBXL_VERSION)) {
        fprintf(stderr, "cannot init xl context\n");
        return;
    }
    libxl_ctx_set_log(&ctx, log_callback, NULL);

    if (domain_qualifier_to_domid(&ctx, dom, &domid) < 0) {
        fprintf(stderr, "%s is an invalid domain identifier\n", dom);
        exit(2);
    }

    disk.backend_domid = 0;
    disk.domid = domid;
    if (phys) {
        p = strchr(phys, ':');
        if (!p) {
            fprintf(stderr, "No type specified, ");
            disk.physpath = phys;
            if (!strncmp(phys, "/dev", 4)) {
                fprintf(stderr, "assuming phy:\n");
                disk.phystype = PHYSTYPE_PHY;
            } else {
                fprintf(stderr, "assuming file:\n");
                disk.phystype = PHYSTYPE_FILE;
            }
        } else {
            *p = '\0';
            p++;
            disk.physpath = p;
            libxl_string_to_phystype(&ctx, phys, &disk.phystype);
        }
    } else {
            disk.physpath = NULL;
            disk.phystype = 0;
    }
    disk.virtpath = virtdev;
    disk.unpluggable = 1;
    disk.readwrite = 0;
    disk.is_cdrom = 1;

    libxl_cdrom_insert(&ctx, domid, &disk);
}

int main_cd_eject(int argc, char **argv)
{
    int opt = 0;
    char *p = NULL, *virtdev;

    while ((opt = getopt(argc, argv, "hn:")) != -1) {
        switch (opt) {
        case 'h':
            help("cd-eject");
            exit(0);
        default:
            fprintf(stderr, "option not supported\n");
            break;
        }
    }
    if (optind >= argc - 1) {
        help("cd-eject");
        exit(2);
    }

    p = argv[optind];
    virtdev = argv[optind + 1];

    cd_insert(p, virtdev, NULL);
    exit(0);
}

int main_cd_insert(int argc, char **argv)
{
    int opt = 0;
    char *p = NULL, *file = NULL, *virtdev;

    while ((opt = getopt(argc, argv, "hn:")) != -1) {
        switch (opt) {
        case 'h':
            help("cd-insert");
            exit(0);
        default:
            fprintf(stderr, "option not supported\n");
            break;
        }
    }
    if (optind >= argc - 2) {
        help("cd-insert");
        exit(2);
    }

    p = argv[optind];
    virtdev = argv[optind + 1];
    file = argv[optind + 2];

    cd_insert(p, virtdev, file);
    exit(0);
}

int main_console(int argc, char **argv)
{
    int opt = 0, cons_num = 0;
    char *p = NULL;

    while ((opt = getopt(argc, argv, "hn:")) != -1) {
        switch (opt) {
        case 'h':
            help("console");
            exit(0);
        case 'n':
            if (optarg) {
                cons_num = strtol(optarg, NULL, 10);
            }
            break;
        default:
            fprintf(stderr, "option not supported\n");
            break;
        }
    }
    if (optind >= argc) {
        help("console");
        exit(2);
    }

    p = argv[optind];

    console(p, cons_num);
    exit(0);
}

void pcilist(char *dom)
{
    struct libxl_ctx ctx;
    uint32_t domid;
    libxl_device_pci *pcidevs;
    int num, i;

    if (libxl_ctx_init(&ctx, LIBXL_VERSION)) {
        fprintf(stderr, "cannot init xl context\n");
        return;
    }
    libxl_ctx_set_log(&ctx, log_callback, NULL);

    if (domain_qualifier_to_domid(&ctx, dom, &domid) < 0) {
        fprintf(stderr, "%s is an invalid domain identifier\n", dom);
        exit(2);
    }
    pcidevs = libxl_device_pci_list(&ctx, domid, &num);
    if (!num)
        return;
    printf("VFn  domain bus  slot func\n");
    for (i = 0; i < num; i++) {
        printf("0x%02x 0x%04x 0x%02x 0x%02x 0x%01x\n", pcidevs[i].vdevfn, pcidevs[i].domain, pcidevs[i].bus, pcidevs[i].dev, pcidevs[i].func);
    }
    free(pcidevs);
}

int main_pcilist(int argc, char **argv)
{
    int opt;
    char *domname = NULL;

    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
        case 'h':
            help("pci-list");
            exit(0);
        default:
            fprintf(stderr, "option not supported\n");
            break;
        }
    }
    if (optind >= argc) {
        help("pci-list");
        exit(2);
    }

    domname = argv[optind];

    pcilist(domname);
    exit(0);
}

void pcidetach(char *dom, char *bdf)
{
    struct libxl_ctx ctx;
    uint32_t domid;
    libxl_device_pci pcidev;
    unsigned int domain, bus, dev, func;

    if (libxl_ctx_init(&ctx, LIBXL_VERSION)) {
        fprintf(stderr, "cannot init xl context\n");
        return;
    }
    libxl_ctx_set_log(&ctx, log_callback, NULL);

    if (domain_qualifier_to_domid(&ctx, dom, &domid) < 0) {
        fprintf(stderr, "%s is an invalid domain identifier\n", dom);
        exit(2);
    }
    memset(&pcidev, 0x00, sizeof(pcidev));
    sscanf(bdf, PCI_BDF, &domain, &bus, &dev, &func);
    libxl_device_pci_init(&pcidev, domain, bus, dev, func, 0);
    libxl_device_pci_remove(&ctx, domid, &pcidev);
}

int main_pcidetach(int argc, char **argv)
{
    int opt;
    char *domname = NULL, *bdf = NULL;

    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
        case 'h':
            help("pci-attach");
            exit(0);
        default:
            fprintf(stderr, "option not supported\n");
            break;
        }
    }
    if (optind >= argc - 1) {
        help("pci-detach");
        exit(2);
    }

    domname = argv[optind];
    bdf = argv[optind + 1];

    pcidetach(domname, bdf);
    exit(0);
}
void pciattach(char *dom, char *bdf, char *vs)
{
    struct libxl_ctx ctx;
    uint32_t domid;
    libxl_device_pci pcidev;
    unsigned int domain, bus, dev, func;

    if (libxl_ctx_init(&ctx, LIBXL_VERSION)) {
        fprintf(stderr, "cannot init xl context\n");
        return;
    }
    libxl_ctx_set_log(&ctx, log_callback, NULL);

    if (domain_qualifier_to_domid(&ctx, dom, &domid) < 0) {
        fprintf(stderr, "%s is an invalid domain identifier\n", dom);
        exit(2);
    }
    memset(&pcidev, 0x00, sizeof(pcidev));
    sscanf(bdf, PCI_BDF, &domain, &bus, &dev, &func);
    libxl_device_pci_init(&pcidev, domain, bus, dev, func, 0);
    libxl_device_pci_add(&ctx, domid, &pcidev);
}

int main_pciattach(int argc, char **argv)
{
    int opt;
    char *domname = NULL, *bdf = NULL, *vs = NULL;

    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
        case 'h':
            help("pci-attach");
            exit(0);
        default:
            fprintf(stderr, "option not supported\n");
            break;
        }
    }
    if (optind >= argc - 1) {
        help("pci-attach");
        exit(2);
    }

    domname = argv[optind];
    bdf = argv[optind + 1];

    if (optind + 1 < argc)
        vs = argv[optind + 2];

    pciattach(domname, bdf, vs);
    exit(0);
}

void pause_domain(char *p)
{
    struct libxl_ctx ctx;
    uint32_t domid;

    if (libxl_ctx_init(&ctx, LIBXL_VERSION)) {
        fprintf(stderr, "cannot init xl context\n");
        return;
    }
    libxl_ctx_set_log(&ctx, log_callback, NULL);

    if (domain_qualifier_to_domid(&ctx, p, &domid) < 0) {
        fprintf(stderr, "%s is an invalid domain identifier\n", p);
        exit(2);
    }
    libxl_domain_pause(&ctx, domid);
}

void unpause_domain(char *p)
{
    struct libxl_ctx ctx;
    uint32_t domid;

    if (libxl_ctx_init(&ctx, LIBXL_VERSION)) {
        fprintf(stderr, "cannot init xl context\n");
        return;
    }
    libxl_ctx_set_log(&ctx, log_callback, NULL);

    if (domain_qualifier_to_domid(&ctx, p, &domid) < 0) {
        fprintf(stderr, "%s is an invalid domain identifier\n", p);
        exit(2);
    }
    libxl_domain_unpause(&ctx, domid);
}

void destroy_domain(char *p)
{
    struct libxl_ctx ctx;
    uint32_t domid;

    if (libxl_ctx_init(&ctx, LIBXL_VERSION)) {
        fprintf(stderr, "cannot init xl context\n");
        return;
    }
    libxl_ctx_set_log(&ctx, log_callback, NULL);

    if (domain_qualifier_to_domid(&ctx, p, &domid) < 0) {
        fprintf(stderr, "%s is an invalid domain identifier\n", p);
        exit(2);
    }
    libxl_domain_destroy(&ctx, domid, 0);
}

void list_domains(void)
{
    struct libxl_ctx ctx;
    struct libxl_dominfo *info;
    int nb_domain, i;

    if (libxl_ctx_init(&ctx, LIBXL_VERSION)) {
        fprintf(stderr, "cannot init xl context\n");
        return;
    }
    libxl_ctx_set_log(&ctx, log_callback, NULL);

    info = libxl_list_domain(&ctx, &nb_domain);

    if (info < 0) {
        fprintf(stderr, "libxl_domain_infolist failed.\n");
        exit(1);
    }
    printf("Name                                        ID   Mem VCPUs\tState\tTime(s)\n");
    for (i = 0; i < nb_domain; i++) {
        printf("%-40s %5d %5lu %5d        %c%c%c %8.1f\n",
                libxl_domid_to_name(&ctx, info[i].domid),
                info[i].domid,
                (unsigned long) (info[i].max_memkb / 1024),
                info[i].vcpu_online,
                info[i].running ? 'r' : '-',
                info[i].paused ? 'p' : '-',
                info[i].dying ? 'd' : '-',
                ((float)info[i].cpu_time / 1e9));
    }
    free(info);
}

void list_vm(void)
{
    struct libxl_ctx ctx;
    struct libxl_vminfo *info;
    int nb_vm, i;

    if (libxl_ctx_init(&ctx, LIBXL_VERSION)) {
        fprintf(stderr, "cannot init xl context\n");
        return;
    }
    libxl_ctx_set_log(&ctx, log_callback, NULL);

    info = libxl_list_vm(&ctx, &nb_vm);

    if (info < 0) {
        fprintf(stderr, "libxl_domain_infolist failed.\n");
        exit(1);
    }
    printf("UUID                                  ID    name\n");
    for (i = 0; i < nb_vm; i++) {
        printf(UUID_FMT "  %d    %-30s\n",
            info[i].uuid[0], info[i].uuid[1], info[i].uuid[2], info[i].uuid[3],
            info[i].uuid[4], info[i].uuid[5], info[i].uuid[6], info[i].uuid[7],
            info[i].uuid[8], info[i].uuid[9], info[i].uuid[10], info[i].uuid[11],
            info[i].uuid[12], info[i].uuid[13], info[i].uuid[14], info[i].uuid[15],
            info[i].domid, libxl_domid_to_name(&ctx, info[i].domid));
    }
    free(info);
}

int save_domain(char *p, char *filename, int checkpoint)
{
    struct libxl_ctx ctx;
    uint32_t domid;
    int fd;

    if (libxl_ctx_init(&ctx, LIBXL_VERSION)) {
        fprintf(stderr, "cannot init xl context\n");
        exit(2);
    }
    libxl_ctx_set_log(&ctx, log_callback, NULL);

    if (domain_qualifier_to_domid(&ctx, p, &domid) < 0) {
        fprintf(stderr, "%s is an invalid domain identifier\n", p);
        exit(2);
    }
    fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "Failed to open temp file %s for writing\n", filename);
        exit(2);
    }
    libxl_domain_suspend(&ctx, NULL, domid, fd);
    close(fd);

    if (checkpoint)
        libxl_domain_unpause(&ctx, domid);
    else
        libxl_domain_destroy(&ctx, domid, 0);

    exit(0);
}

int main_restore(int argc, char **argv)
{
    char *checkpoint_file = NULL;
    char *config_file = NULL;
    int paused = 0, debug = 0, daemonize = 1;
    int opt;

    while ((opt = getopt(argc, argv, "hpde")) != -1) {
        switch (opt) {
        case 'p':
            paused = 1;
            break;
        case 'd':
            debug = 1;
            break;
        case 'e':
            daemonize = 0;
            break;
        case 'h':
            help("restore");
            exit(0);
        default:
            fprintf(stderr, "option not supported\n");
            break;
        }
    }

    if (optind >= argc - 1) {
        help("restore");
        exit(2);
    }

    config_file = argv[optind];
    checkpoint_file = argv[optind + 1];
    create_domain(debug, daemonize, config_file, checkpoint_file, paused);
    exit(0);
}

int main_save(int argc, char **argv)
{
    char *filename = NULL, *p = NULL;
    int checkpoint = 0;
    int opt;

    while ((opt = getopt(argc, argv, "hc")) != -1) {
        switch (opt) {
        case 'c':
            checkpoint = 1;
            break;
        case 'h':
            help("save");
            exit(0);
        default:
            fprintf(stderr, "option not supported\n");
            break;
        }
    }

    if (optind >= argc - 1) {
        help("save");
        exit(2);
    }

    p = argv[optind];
    filename = argv[optind + 1];
    save_domain(p, filename, checkpoint);
    exit(0);
}

int main_pause(int argc, char **argv)
{
    int opt;
    char *p;
    

    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
        case 'h':
            help("pause");
            exit(0);
        default:
            fprintf(stderr, "option not supported\n");
            break;
        }
    }
    if (optind >= argc) {
        help("pause");
        exit(2);
    }

    p = argv[optind];

    pause_domain(p);
    exit(0);
}

int main_unpause(int argc, char **argv)
{
    int opt;
    char *p;
    

    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
        case 'h':
            help("unpause");
            exit(0);
        default:
            fprintf(stderr, "option not supported\n");
            break;
        }
    }
    if (optind >= argc) {
        help("unpause");
        exit(2);
    }

    p = argv[optind];

    unpause_domain(p);
    exit(0);
}

int main_destroy(int argc, char **argv)
{
    int opt;
    char *p;

    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
        case 'h':
            help("destroy");
            exit(0);
        default:
            fprintf(stderr, "option not supported\n");
            break;
        }
    }
    if (optind >= argc) {
        help("destroy");
        exit(2);
    }

    p = argv[optind];

    destroy_domain(p);
    exit(0);
}

int main_list(int argc, char **argv)
{
    int opt;

    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
        case 'h':
            help("list");
            exit(0);
        default:
            fprintf(stderr, "option not supported\n");
            break;
        }
    }

    list_domains();
    exit(0);
}

int main_list_vm(int argc, char **argv)
{
    int opt;

    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
        case 'h':
            help("list-vm");
            exit(0);
        default:
            fprintf(stderr, "option not supported\n");
            break;
        }
    }

    list_vm();
    exit(0);
}

int main_create(int argc, char **argv)
{
    char *filename = NULL;
    int debug = 0, daemonize = 1;
    int opt;

    while ((opt = getopt(argc, argv, "hde")) != -1) {
        switch (opt) {
        case 'd':
            debug = 1;
            break;
        case 'e':
            daemonize = 0;
            break;
        case 'h':
            help("create");
            exit(0);
        default:
            fprintf(stderr, "option not supported\n");
            break;
        }
    }

    if (optind >= argc) {
        help("create");
        exit(2);
    }

    filename = argv[optind];
    create_domain(debug, daemonize, filename, NULL, 0);
    exit(0);
}

void button_press(char *p, char *b)
{
    struct libxl_ctx ctx;
    uint32_t domid;
    libxl_button button;

    libxl_ctx_init(&ctx, LIBXL_VERSION);
    libxl_ctx_set_log(&ctx, log_callback, NULL);

    if (domain_qualifier_to_domid(&ctx, p, &domid) < 0) {
        fprintf(stderr, "%s is an invalid domain identifier\n", p);
        exit(2);
    }

    if (!strcmp(b, "power")) {
        button = POWER_BUTTON;
    } else if (!strcmp(b, "sleep")) {
        button = SLEEP_BUTTON;
    } else {
        fprintf(stderr, "%s is an invalid button identifier\n", b);
        exit(2);
    }

    libxl_button_press(&ctx, domid, button);
}

int main_button_press(int argc, char **argv)
{
    int opt;
    char *p;
    char *b;

    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
        case 'h':
            help("button-press");
            exit(0);
        default:
            fprintf(stderr, "option not supported\n");
            break;
        }
    }
    if (optind >= argc - 1) {
        help("button-press");
        exit(2);
    }

    p = argv[optind];
    b = argv[optind + 1];

    button_press(p, b);
    exit(0);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        help(NULL);
        exit(1);
    }

    srand(time(0));

    if (!strcmp(argv[1], "create")) {
        main_create(argc - 1, argv + 1);
    } else if (!strcmp(argv[1], "list")) {
        main_list(argc - 1, argv + 1);
    } else if (!strcmp(argv[1], "list-vm")) {
        main_list_vm(argc - 1, argv + 1);
    } else if (!strcmp(argv[1], "destroy")) {
        main_destroy(argc - 1, argv + 1);
    } else if (!strcmp(argv[1], "pci-attach")) {
        main_pciattach(argc - 1, argv + 1);
    } else if (!strcmp(argv[1], "pci-detach")) {
        main_pcidetach(argc - 1, argv + 1);
    } else if (!strcmp(argv[1], "pci-list")) {
        main_pcilist(argc - 1, argv + 1);
    } else if (!strcmp(argv[1], "pause")) {
        main_pause(argc - 1, argv + 1);
    } else if (!strcmp(argv[1], "unpause")) {
        main_unpause(argc - 1, argv + 1);
    } else if (!strcmp(argv[1], "console")) {
        main_console(argc - 1, argv + 1);
    } else if (!strcmp(argv[1], "save")) {
        main_save(argc - 1, argv + 1);
    } else if (!strcmp(argv[1], "restore")) {
        main_restore(argc - 1, argv + 1);
    } else if (!strcmp(argv[1], "cd-insert")) {
        main_cd_insert(argc - 1, argv + 1);
    } else if (!strcmp(argv[1], "cd-eject")) {
        main_cd_eject(argc - 1, argv + 1);
    } else if (!strcmp(argv[1], "mem-set")) {
        main_memset(argc - 1, argv + 1);
    } else if (!strcmp(argv[1], "button-press")) {
        main_button_press(argc - 1, argv + 1);
    } else if (!strcmp(argv[1], "help")) {
        if (argc > 2)
            help(argv[2]);
        else
            help(NULL);
        exit(0);
    } else {
        fprintf(stderr, "command not implemented\n");
        exit(1);
    }
}

