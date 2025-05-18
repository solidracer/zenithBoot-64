#ifndef _zb_h
#define _zb_h

typedef enum {
    PF_RGBR_32BBP,
    PF_BGRR_32BBP
} zenith_fb_pixel_format_t;

typedef struct {
    int type;
    unsigned long physaddr, virtaddr;
    unsigned long numpage, attr;
} efi_memory_descr_t;

typedef struct {
    void *fb;
    int width, height, ppsl;
    zenith_fb_pixel_format_t pixel_format;
} __attribute__((packed)) zenith_boot_info_t;

typedef struct {
    /* memory map */
    unsigned long entries, desc_size;
    efi_memory_descr_t *map;
} __attribute__((packed)) zenith_memory_map_t;

#endif
