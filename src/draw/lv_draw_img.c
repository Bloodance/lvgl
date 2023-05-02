/**
 * @file lv_draw_img.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_draw_img.h"
#include "lv_img_cache.h"
#include "../core/lv_disp.h"
#include "../misc/lv_log.h"
#include "../core/lv_refr.h"
#include "../misc/lv_mem.h"
#include "../misc/lv_math.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
LV_ATTRIBUTE_FAST_MEM static lv_res_t decode_and_draw(lv_layer_t * layer, const lv_draw_img_dsc_t * draw_dsc,
                                                      const lv_area_t * coords, const void * src);

static void show_error(lv_layer_t * layer, const lv_area_t * coords, const char * msg);
static void draw_cleanup(_lv_img_cache_entry_t * cache);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_draw_img_dsc_init(lv_draw_img_dsc_t * dsc)
{
    lv_memzero(dsc, sizeof(lv_draw_img_dsc_t));
    dsc->recolor = lv_color_black();
    dsc->opa = LV_OPA_COVER;
    dsc->zoom = LV_ZOOM_NONE;
    dsc->antialias = LV_COLOR_DEPTH > 8 ? 1 : 0;
}


void lv_draw_layer(lv_layer_t * layer, const lv_draw_img_dsc_t * dsc, const lv_area_t * coords)
{
    lv_draw_task_t * t = lv_draw_add_task(layer, coords);

    t->draw_dsc = lv_malloc(sizeof(*dsc));
    lv_memcpy(t->draw_dsc, dsc, sizeof(*dsc));
    t->type = LV_DRAW_TASK_TYPE_LAYER;
    t->state = LV_DRAW_TASK_STATE_WAITING;

    lv_layer_t * layer_to_draw = (lv_layer_t *)dsc->src;
    layer_to_draw->all_tasks_added = true;

    lv_draw_dsc_base_t * base_dsc = t->draw_dsc;
    base_dsc->layer = layer;

    if(base_dsc->obj && lv_obj_has_flag(base_dsc->obj, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS)) {
        /*Disable sending LV_EVENT_DRAW_TASK_ADDED first to avoid triggering recursive
         *event calls due draw task adds in the event*/
        lv_obj_clear_flag(base_dsc->obj, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
        lv_obj_send_event(dsc->base.obj, LV_EVENT_DRAW_TASK_ADDED, t);
        lv_obj_add_flag(base_dsc->obj, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    }

    lv_draw_dispatch();
}


void lv_draw_img(lv_layer_t * layer, const lv_draw_img_dsc_t * dsc, const lv_area_t * coords)
{
    if(dsc->src == NULL) {
        LV_LOG_WARN("Image draw: src is NULL");
        return;
    }
    if(dsc->opa <= LV_OPA_MIN) return;

    lv_draw_task_t * t = lv_draw_add_task(layer, coords);

    t->draw_dsc = lv_malloc(sizeof(*dsc));
    lv_memcpy(t->draw_dsc, dsc, sizeof(*dsc));
    t->type = LV_DRAW_TASK_TYPE_IMAGE;

    lv_draw_dsc_base_t * base_dsc = t->draw_dsc;
    base_dsc->layer = layer;

    if(base_dsc->obj && lv_obj_has_flag(base_dsc->obj, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS)) {
        lv_obj_send_event(base_dsc->obj, LV_EVENT_DRAW_TASK_ADDED, t);
    }


    lv_draw_dispatch();
}

/**
 * Get the type of an image source
 * @param src pointer to an image source:
 *  - pointer to an 'lv_img_t' variable (image stored internally and compiled into the code)
 *  - a path to a file (e.g. "S:/folder/image.bin")
 *  - or a symbol (e.g. LV_SYMBOL_CLOSE)
 * @return type of the image source LV_IMG_SRC_VARIABLE/FILE/SYMBOL/UNKNOWN
 */
lv_img_src_t lv_img_src_get_type(const void * src)
{
    lv_img_src_t img_src_type = LV_IMG_SRC_UNKNOWN;

    if(src == NULL) return img_src_type;
    const uint8_t * u8_p = src;

    /*The first byte shows the type of the image source*/
    if(u8_p[0] >= 0x20 && u8_p[0] <= 0x7F) {
        img_src_type = LV_IMG_SRC_FILE; /*If it's an ASCII character then it's file name*/
    }
    else if(u8_p[0] >= 0x80) {
        img_src_type = LV_IMG_SRC_SYMBOL; /*Symbols begins after 0x7F*/
    }
    else {
        img_src_type = LV_IMG_SRC_VARIABLE; /*`lv_img_dsc_t` is draw to the first byte < 0x20*/
    }

    if(LV_IMG_SRC_UNKNOWN == img_src_type) {
        LV_LOG_WARN("unknown image type");
    }

    return img_src_type;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

LV_ATTRIBUTE_FAST_MEM static lv_res_t decode_and_draw(lv_layer_t * layer, const lv_draw_img_dsc_t * draw_dsc,
                                                      const lv_area_t * coords, const void * src)
{
    //    if(draw_dsc->opa <= LV_OPA_MIN) return LV_RES_OK;
    //
    //    _lv_img_cache_entry_t * cdsc = _lv_img_cache_open(src, draw_dsc->recolor, draw_dsc->frame_id);
    //
    //    if(cdsc == NULL) return LV_RES_INV;
    //
    //    if(cdsc->dec_dsc.error_msg != NULL) {
    //        LV_LOG_WARN("Image draw error");
    //        show_error(layer, coords, cdsc->dec_dsc.error_msg);
    //        draw_cleanup(cdsc);
    //        return LV_RES_INV;
    //    }
    //
    //    lv_color_format_t cf = cdsc->dec_dsc.header.cf;
    //    lv_draw_img_sup_t sup;
    //    sup.palette = cdsc->dec_dsc.palette;
    //    sup.palette_size = cdsc->dec_dsc.palette_size;
    //    sup.alpha_color = cdsc->dec_dsc.color;
    //    sup.chroma_key_color = lv_color_hex(0x00ff00);
    //    sup.chroma_keyed = cf == LV_COLOR_FORMAT_NATIVE_CHROMA_KEYED ? 1 : 0;
    //
    //    /*The decoder could open the image and gave the entire uncompressed image.
    //     *Just draw it!*/
    //    if(cdsc->dec_dsc.img_data) {
    //        lv_area_t map_area_rot;
    //        lv_area_copy(&map_area_rot, coords);
    //        if(draw_dsc->angle || draw_dsc->zoom != LV_ZOOM_NONE) {
    //            int32_t w = lv_area_get_width(coords);
    //            int32_t h = lv_area_get_height(coords);
    //
    //            _lv_img_buf_get_transformed_area(&map_area_rot, w, h, draw_dsc->angle, draw_dsc->zoom, &draw_dsc->pivot);
    //
    //            map_area_rot.x1 += coords->x1;
    //            map_area_rot.y1 += coords->y1;
    //            map_area_rot.x2 += coords->x1;
    //            map_area_rot.y2 += coords->y1;
    //        }
    //
    //        lv_area_t clip_com; /*Common area of mask and coords*/
    //        bool union_ok;
    //        union_ok = _lv_area_intersect(&clip_com, layer->clip_area, &map_area_rot);
    //        /*Out of mask. There is nothing to draw so the image is drawn successfully.*/
    //        if(union_ok == false) {
    //            draw_cleanup(cdsc);
    //            return LV_RES_OK;
    //        }
    //
    //        const lv_area_t * clip_area_ori = layer->clip_area;
    //        layer->clip_area = &clip_com;
    //        lv_draw_img_decoded(layer, draw_dsc, coords, cdsc->dec_dsc.img_data, &sup, cf);
    //        layer->clip_area = clip_area_ori;
    //    }
    //    /*The whole uncompressed image is not available. Try to read it line-by-line*/
    //    else {
    //        lv_area_t mask_com; /*Common area of mask and coords*/
    //        bool union_ok;
    //        union_ok = _lv_area_intersect(&mask_com, layer->clip_area, coords);
    //        /*Out of mask. There is nothing to draw so the image is drawn successfully.*/
    //        if(union_ok == false) {
    //            draw_cleanup(cdsc);
    //            return LV_RES_OK;
    //        }
    //
    //        int32_t width = lv_area_get_width(&mask_com);
    //
    //        uint8_t  * buf = lv_malloc(lv_area_get_width(&mask_com) * LV_COLOR_FORMAT_NATIVE_ALPHA_SIZE);
    //        const lv_area_t * clip_area_ori = layer->clip_area;
    //        lv_area_t line;
    //        lv_area_copy(&line, &mask_com);
    //        lv_area_set_height(&line, 1);
    //        int32_t x = mask_com.x1 - coords->x1;
    //        int32_t y = mask_com.y1 - coords->y1;
    //        int32_t row;
    //        lv_res_t read_res;
    //        for(row = mask_com.y1; row <= mask_com.y2; row++) {
    //            lv_area_t mask_line;
    //            union_ok = _lv_area_intersect(&mask_line, clip_area_ori, &line);
    //            if(union_ok == false) continue;
    //
    //            read_res = lv_img_decoder_read_line(&cdsc->dec_dsc, x, y, width, buf);
    //            if(read_res != LV_RES_OK) {
    //                lv_img_decoder_close(&cdsc->dec_dsc);
    //                LV_LOG_WARN("Image draw can't read the line");
    //                lv_free(buf);
    //                draw_cleanup(cdsc);
    //                layer->clip_area = clip_area_ori;
    //                return LV_RES_INV;
    //            }
    //
    //            layer->clip_area = &mask_line;
    //            lv_draw_img_decoded(layer, draw_dsc, &line, buf, &sup, cf);
    //            line.y1++;
    //            line.y2++;
    //            y++;
    //        }
    //        layer->clip_area = clip_area_ori;
    //        lv_free(buf);
    //    }
    //
    //    draw_cleanup(cdsc);
    //    return LV_RES_OK;
}


static void show_error(lv_layer_t * layer, const lv_area_t * coords, const char * msg)
{
    //    lv_draw_rect_dsc_t rect_dsc;
    //    lv_draw_rect_dsc_init(&rect_dsc);
    //    rect_dsc.bg_color = lv_color_white();
    //    lv_draw_rect(layer, &rect_dsc, coords);
    //
    //    lv_draw_label_dsc_t label_dsc;
    //    lv_draw_label_dsc_init(&label_dsc);
    //    lv_draw_label(layer, &label_dsc, coords, msg, NULL);
}

static void draw_cleanup(_lv_img_cache_entry_t * cache)
{
    /*Automatically close images with no caching*/
#if LV_IMG_CACHE_DEF_SIZE == 0
    lv_img_decoder_close(&cache->dec_dsc);
#else
    LV_UNUSED(cache);
#endif
}
