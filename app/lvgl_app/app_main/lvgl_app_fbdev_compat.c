#include "app_main/lvgl_app_fbdev_compat.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/config.h>
#include <nuttx/cache.h>
#include <nuttx/video/fb.h>

#include <lvgl/lvgl.h>

#ifndef MAP_FILE
#  define MAP_FILE 0
#endif

#define LVGL_APP_FBDEV_COMPAT_TAG "app_fb"
#define LVGL_APP_AIC_MPP_FMT_RGB565 0x0e
#define LVGL_APP_FBDEV_COMPAT_PARTIAL_ROWS 60u

struct lvgl_app_fbdev_compat_s
{
  int fd;
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;
  void *mem;
  size_t map_len;
  size_t page_len;
  bool double_buffer;
  uint32_t display_buf_id;
  void *draw_mem;
  size_t draw_len;
  uint32_t draw_stride;
};

static void lvgl_app_fbdev_compat_flush(lv_display_t *disp,
                                        const lv_area_t *area,
                                        uint8_t *px_map);
static void lvgl_app_fbdev_compat_release(lv_event_t *event);

static bool lvgl_app_fbdev_compat_is_rgb565(
  const struct fb_videoinfo_s *vinfo,
  const struct fb_planeinfo_s *pinfo)
{
  uint32_t min_stride;

  if (vinfo == NULL || pinfo == NULL)
    {
      return false;
    }

  min_stride = (uint32_t)vinfo->xres * 2u;
  return vinfo->xres > 0 && vinfo->yres > 0 && vinfo->nplanes == 1 &&
         pinfo->bpp == 16 && pinfo->stride >= min_stride &&
         pinfo->fblen >= (size_t)pinfo->stride * vinfo->yres;
}

bool lvgl_app_fbdev_compat_matches(const char *path)
{
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;
  bool matches;
  int fd;

  if (path == NULL || path[0] == '\0')
    {
      return false;
    }

  fd = open(path, O_RDWR | O_CLOEXEC);
  if (fd < 0)
    {
      return false;
    }

  matches = ioctl(fd, FBIOGET_VIDEOINFO,
                  (unsigned long)((uintptr_t)&vinfo)) >= 0;
  if (matches)
    {
      memset(&pinfo, 0, sizeof(pinfo));
      matches = ioctl(fd, FBIOGET_PLANEINFO,
                      (unsigned long)((uintptr_t)&pinfo)) >= 0;
    }

  if (matches)
    {
      matches = vinfo.fmt == LVGL_APP_AIC_MPP_FMT_RGB565 &&
                lvgl_app_fbdev_compat_is_rgb565(&vinfo, &pinfo);
    }

  close(fd);
  return matches;
}

static bool lvgl_app_fbdev_compat_clip_area(
  const struct lvgl_app_fbdev_compat_s *fb,
  const lv_area_t *src,
  lv_area_t *dst)
{
  if (fb == NULL || src == NULL || dst == NULL ||
      src->x2 < 0 || src->y2 < 0 ||
      src->x1 >= fb->vinfo.xres || src->y1 >= fb->vinfo.yres)
    {
      return false;
    }

  *dst = *src;
  if (dst->x1 < 0)
    {
      dst->x1 = 0;
    }
  if (dst->y1 < 0)
    {
      dst->y1 = 0;
    }
  if (dst->x2 >= fb->vinfo.xres)
    {
      dst->x2 = fb->vinfo.xres - 1;
    }
  if (dst->y2 >= fb->vinfo.yres)
    {
      dst->y2 = fb->vinfo.yres - 1;
    }

  return dst->x1 <= dst->x2 && dst->y1 <= dst->y2;
}

static void lvgl_app_fbdev_compat_update_area(
  struct lvgl_app_fbdev_compat_s *fb,
  uint32_t buf_id,
  const lv_area_t *area)
{
#ifdef CONFIG_FB_UPDATE
  struct fb_area_s fb_area;

  if (area != NULL)
    {
      fb_area.x = area->x1;
      fb_area.y = buf_id * fb->vinfo.yres + area->y1;
      fb_area.w = lv_area_get_width(area);
      fb_area.h = lv_area_get_height(area);
    }
  else
    {
      fb_area.x = 0;
      fb_area.y = buf_id * fb->vinfo.yres;
      fb_area.w = fb->vinfo.xres;
      fb_area.h = fb->vinfo.yres;
    }

  if (ioctl(fb->fd, FBIO_UPDATE, (unsigned long)((uintptr_t)&fb_area)) < 0)
    {
      syslog(LOG_WARNING, "%s: FBIO_UPDATE failed errno=%d\n",
             LVGL_APP_FBDEV_COMPAT_TAG, errno);
    }
#else
  (void)fb;
  (void)buf_id;
  (void)area;
#endif
}

static void lvgl_app_fbdev_compat_clean_area(
  const struct lvgl_app_fbdev_compat_s *fb,
  uint32_t buf_id,
  const lv_area_t *area)
{
  uintptr_t page_start;
  uintptr_t line_start;
  uint32_t line_bytes;
  int32_t y;

  if (fb == NULL || area == NULL)
    {
      return;
    }

  page_start = (uintptr_t)fb->mem + (size_t)buf_id * fb->page_len;
  line_bytes = (uint32_t)lv_area_get_width(area) * 2u;
  for (y = area->y1; y <= area->y2; y++)
    {
      line_start = page_start + (size_t)y * fb->pinfo.stride +
                   (size_t)area->x1 * 2u;
      up_clean_dcache(line_start, line_start + line_bytes);
    }
}

static void lvgl_app_fbdev_compat_copy_frame(
  struct lvgl_app_fbdev_compat_s *fb,
  uint32_t buf_id,
  const uint8_t *src)
{
  uint8_t *dst;
  uint32_t y;

  dst = (uint8_t *)fb->mem + (size_t)buf_id * fb->page_len;

  for (y = 0; y < fb->vinfo.yres; y++)
    {
      memcpy(dst, src, fb->draw_stride);
      dst += fb->pinfo.stride;
      src += fb->draw_stride;
    }
}

static void lvgl_app_fbdev_compat_copy_area(
  struct lvgl_app_fbdev_compat_s *fb,
  const lv_area_t *area,
  const lv_area_t *clipped,
  const uint8_t *src)
{
  uint8_t *dst;
  uint32_t src_stride;
  uint32_t line_bytes;
  int32_t y;

  src_stride = (uint32_t)lv_area_get_width(area) * 2u;
  line_bytes = (uint32_t)lv_area_get_width(clipped) * 2u;
  src += (size_t)(clipped->y1 - area->y1) * src_stride +
         (size_t)(clipped->x1 - area->x1) * 2u;
  dst = (uint8_t *)fb->mem + (size_t)clipped->y1 * fb->pinfo.stride +
        (size_t)clipped->x1 * 2u;

  for (y = clipped->y1; y <= clipped->y2; y++)
    {
      memcpy(dst, src, line_bytes);
      dst += fb->pinfo.stride;
      src += src_stride;
    }
}

static void lvgl_app_fbdev_compat_clean_frame(
  const struct lvgl_app_fbdev_compat_s *fb,
  uint32_t buf_id)
{
  lv_area_t area;

  area.x1 = 0;
  area.y1 = 0;
  area.x2 = fb->vinfo.xres - 1;
  area.y2 = fb->vinfo.yres - 1;
  lvgl_app_fbdev_compat_clean_area(fb, buf_id, &area);
}

static void lvgl_app_fbdev_compat_flush(lv_display_t *disp,
                                        const lv_area_t *area,
                                        uint8_t *px_map)
{
  struct lvgl_app_fbdev_compat_s *fb;
  lv_area_t clipped;
  uint32_t target_buf_id;
  int ret;

  fb = lv_display_get_driver_data(disp);
  if (fb == NULL || fb->mem == NULL || px_map == NULL ||
      !lvgl_app_fbdev_compat_clip_area(fb, area, &clipped))
    {
      lv_display_flush_ready(disp);
      return;
    }

  if (fb->double_buffer)
    {
      if (!lv_display_flush_is_last(disp))
        {
          lv_display_flush_ready(disp);
          return;
        }

      target_buf_id = fb->display_buf_id ^ 1u;
      lvgl_app_fbdev_compat_copy_frame(fb, target_buf_id, px_map);
      lvgl_app_fbdev_compat_clean_frame(fb, target_buf_id);
      lvgl_app_fbdev_compat_update_area(fb, target_buf_id, NULL);

      fb->pinfo.yoffset = target_buf_id * fb->vinfo.yres;
      ret = ioctl(fb->fd, FBIOPAN_DISPLAY,
                  (unsigned long)((uintptr_t)&fb->pinfo));
      if (ret < 0)
        {
          syslog(LOG_WARNING, "%s: FBIOPAN_DISPLAY failed errno=%d\n",
                 LVGL_APP_FBDEV_COMPAT_TAG, errno);
        }
      else
        {
          fb->display_buf_id = target_buf_id;
        }
    }
  else
    {
      lvgl_app_fbdev_compat_copy_area(fb, area, &clipped, px_map);
      lvgl_app_fbdev_compat_clean_area(fb, 0, &clipped);
      lvgl_app_fbdev_compat_update_area(fb, 0, &clipped);
    }

#ifdef CONFIG_FB_SYNC
  if (ioctl(fb->fd, FBIO_WAITFORVSYNC, 0) < 0)
    {
      syslog(LOG_WARNING, "%s: FBIO_WAITFORVSYNC failed errno=%d\n",
             LVGL_APP_FBDEV_COMPAT_TAG, errno);
    }
#endif

  lv_display_flush_ready(disp);
}

static void lvgl_app_fbdev_compat_release(lv_event_t *event)
{
  lv_display_t *disp = lv_event_get_user_data(event);
  struct lvgl_app_fbdev_compat_s *fb;

  fb = lv_display_get_driver_data(disp);
  if (fb == NULL)
    {
      return;
    }

  lv_display_set_driver_data(disp, NULL);
  lv_display_set_flush_cb(disp, NULL);

  if (fb->mem != NULL && fb->mem != MAP_FAILED)
    {
      munmap(fb->mem, fb->map_len);
      fb->mem = NULL;
    }

  if (fb->draw_mem != NULL)
    {
      free(fb->draw_mem);
      fb->draw_mem = NULL;
    }

  if (fb->fd >= 0)
    {
      close(fb->fd);
      fb->fd = -1;
    }

  lv_free(fb);
}

lv_display_t *lvgl_app_fbdev_compat_create(const char *path)
{
  struct lvgl_app_fbdev_compat_s *fb;
  lv_display_t *disp;
  int ret;

  if (path == NULL || path[0] == '\0')
    {
      return NULL;
    }

  fb = lv_malloc_zeroed(sizeof(*fb));
  if (fb == NULL)
    {
      return NULL;
    }

  fb->fd = -1;
  fb->fd = open(path, O_RDWR | O_CLOEXEC);
  if (fb->fd < 0)
    {
      syslog(LOG_ERR, "%s: open %s failed errno=%d\n",
             LVGL_APP_FBDEV_COMPAT_TAG, path, errno);
      goto errout;
    }

  ret = ioctl(fb->fd, FBIOGET_VIDEOINFO,
              (unsigned long)((uintptr_t)&fb->vinfo));
  if (ret < 0)
    {
      syslog(LOG_ERR, "%s: FBIOGET_VIDEOINFO failed errno=%d\n",
             LVGL_APP_FBDEV_COMPAT_TAG, errno);
      goto errout;
    }

  memset(&fb->pinfo, 0, sizeof(fb->pinfo));
  ret = ioctl(fb->fd, FBIOGET_PLANEINFO,
              (unsigned long)((uintptr_t)&fb->pinfo));
  if (ret < 0)
    {
      syslog(LOG_ERR, "%s: FBIOGET_PLANEINFO failed errno=%d\n",
             LVGL_APP_FBDEV_COMPAT_TAG, errno);
      goto errout;
    }

  if (!lvgl_app_fbdev_compat_is_rgb565(&fb->vinfo, &fb->pinfo))
    {
      syslog(LOG_ERR,
             "%s: unsupported fb shape fmt=%u bpp=%u stride=%u %ux%u\n",
             LVGL_APP_FBDEV_COMPAT_TAG, fb->vinfo.fmt, fb->pinfo.bpp,
             fb->pinfo.stride, fb->vinfo.xres, fb->vinfo.yres);
      goto errout;
    }

  fb->map_len = fb->pinfo.fblen;
  fb->page_len = (size_t)fb->pinfo.stride * fb->vinfo.yres;
  fb->double_buffer =
    fb->pinfo.yres_virtual >= fb->vinfo.yres * 2u &&
    fb->map_len >= fb->page_len * 2u;
  fb->display_buf_id = 0;
  fb->mem = mmap(NULL, fb->map_len, PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_FILE, fb->fd, 0);
  if (fb->mem == MAP_FAILED)
    {
      syslog(LOG_ERR, "%s: mmap failed errno=%d\n",
             LVGL_APP_FBDEV_COMPAT_TAG, errno);
      fb->mem = NULL;
      goto errout;
    }

  fb->draw_stride = (uint32_t)fb->vinfo.xres * 2u;
  fb->draw_len = fb->double_buffer ?
                 (size_t)fb->draw_stride * fb->vinfo.yres :
                 (size_t)fb->draw_stride *
                 (fb->vinfo.yres < LVGL_APP_FBDEV_COMPAT_PARTIAL_ROWS ?
                  fb->vinfo.yres : LVGL_APP_FBDEV_COMPAT_PARTIAL_ROWS);
  fb->draw_mem = malloc(fb->draw_len);
  if (fb->draw_mem == NULL)
    {
      syslog(LOG_ERR, "%s: draw buffer alloc failed len=%lu\n",
             LVGL_APP_FBDEV_COMPAT_TAG, (unsigned long)fb->draw_len);
      goto errout;
    }

  disp = lv_display_create(fb->vinfo.xres, fb->vinfo.yres);
  if (disp == NULL)
    {
      goto errout;
    }

  lv_display_set_driver_data(disp, fb);
  lv_display_add_event_cb(disp, lvgl_app_fbdev_compat_release,
                          LV_EVENT_DELETE, disp);
  lv_display_set_flush_cb(disp, lvgl_app_fbdev_compat_flush);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, fb->draw_mem, NULL, fb->draw_len,
                         fb->double_buffer ? LV_DISPLAY_RENDER_MODE_DIRECT :
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_resolution(disp, fb->vinfo.xres, fb->vinfo.yres);
  lv_display_set_default(disp);

  syslog(LOG_INFO,
         "%s: using RGB565 fb compat path %s fmt=%u bpp=%u stride=%u %ux%u drawbuf=%lu double=%d mode=%s\n",
         LVGL_APP_FBDEV_COMPAT_TAG, path, fb->vinfo.fmt, fb->pinfo.bpp,
         fb->pinfo.stride, fb->vinfo.xres, fb->vinfo.yres,
         (unsigned long)fb->draw_len, fb->double_buffer ? 1 : 0,
         fb->double_buffer ? "direct" : "partial");
  return disp;

errout:
  if (fb->mem != NULL && fb->mem != MAP_FAILED)
    {
      munmap(fb->mem, fb->map_len);
    }

  if (fb->draw_mem != NULL)
    {
      free(fb->draw_mem);
    }

  if (fb->fd >= 0)
    {
      close(fb->fd);
    }

  lv_free(fb);
  return NULL;
}
