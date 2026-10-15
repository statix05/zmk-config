#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <lvgl.h>
#include "sofle_status_render.h"
#include "sofle_union_logo.h"
static lv_color_t pixels[128*32];
static unsigned char output[128*32];
static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *data) {
    for(int y=area->y1;y<=area->y2;y++) {
        for(int x=area->x1;x<=area->x2;x++) {
            assert(x>=0 && x<128 && y>=0 && y<32);
            output[y*128+x] = data->full ? 1 : 0;
            data++;
        }
    }
    lv_disp_flush_ready(driver);
}
int main(void) {
    lv_init();
    static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer,pixels,NULL,128*32);
    static lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res=128; driver.ver_res=32;
    driver.draw_buf=&buffer; driver.flush_cb=flush;
    lv_disp_drv_register(&driver);
    lv_obj_t *screen=lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen,128,32);
    lv_obj_clear_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *image=lv_img_create(screen);
    lv_obj_remove_style_all(image);
    static uint8_t frame[SOFLE_FRAME_BYTES];
    static lv_img_dsc_t desc={.header.cf=LV_IMG_CF_INDEXED_1BIT,.header.w=128,.header.h=32,
                              .data_size=sizeof(frame),.data=frame};
    struct sofle_status_view state={.central=true,.battery_valid=true,.battery_percent=72,
                                    .connected=true,.bonded=true,.layer="BAS"};
    for(int step=0;step<4;step++) {
        bool inverted=step%2;
        state.usb_powered=step>=2;
        sofle_status_render(frame,&state,sofle_union_logo.data+8,inverted);
        lv_img_cache_invalidate_src(&desc);
        lv_img_set_src(image,&desc);
        lv_obj_set_pos(image,0,0);
        lv_scr_load(screen);
        lv_obj_invalidate(image);
        lv_refr_now(NULL);
        int count=0;
        for(int y=0;y<32;y++) for(int x=0;x<128;x++) {
            int bit=!!(frame[8+y*16+x/8] & (0x80>>(x%8)));
            int expected=bit^inverted;
            if(output[y*128+x] != expected) count++;
        }
        printf("LVGL indexed1bit pass=%d inverted=%d mismatches=%d children=%u\n",
                step,inverted,count,(unsigned)lv_obj_get_child_cnt(screen));
        assert(count==0);
        assert(lv_obj_get_child_cnt(screen)==1);
    }
    return 0;
}
