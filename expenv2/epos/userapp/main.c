/*
 * vim: filetype=c:fenc=utf-8:ts=4:et:sw=4:sts=4
 */
#include <inttypes.h>
#include <stddef.h>
#include <math.h>
#include <stdio.h>
#include <sys/mman.h>
#include <syscall.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include "graphics.h"

#define SORT_N          80
#define THREAD_COUNT    4
#define STACK_SIZE      (1024 * 1024)
#define FOOTER_HEIGHT   100
#define EQUAL_COUNT     20

extern void *tlsf_create_with_pool(void* mem, size_t bytes);
extern void *g_heap;

/**
 * GCC insists on __main
 *    http://gcc.gnu.org/onlinedocs/gccint/Collect2.html
 */
typedef struct _Elem {
    int value;          
    int origin_id;      
    COLORREF color;     
} Elem;

typedef struct _SortTask {
    char name[16];
    Elem *arr;
    int n;

    int left, right, top, bottom;      
    int footer_top, footer_bottom;     

    unsigned char *stack;
    int tid;
    int algo_type;                     

    time_t start_sec;
    unsigned int start_msec;
    time_t end_sec;
    unsigned int end_msec;

    char start_text[64];
    char end_text[64];

    int done;
} SortTask;

static COLORREF rainbow_color(int i);
static void swap_elem(Elem *a, Elem *b);
static void shuffle(Elem *a, int n);
static void copy_array(Elem *dst, Elem *src, int n);
static void generate_base_array(Elem *base, int n);

static void small_delay(void);
static void clear_rect(int l, int t, int r, int b, COLORREF c);
static void draw_border(int l, int t, int r, int b, COLORREF c);
static void draw_region(SortTask *task);

static void bubble_sort_visual(SortTask *task);
static void insertion_sort_visual(SortTask *task);
static void quick_sort_visual(SortTask *task, int l, int r);
static void heap_sort_visual(SortTask *task);

static int is_leap(int year);
static int days_of_month(int year, int month);
static void unix_to_datetime(time_t sec,
                             int *year, int *month, int *day,
                             int *hour, int *minute, int *second);
static void format_precise_time(time_t sec, unsigned int msec, char *buf);

static void draw_char8x16(int x, int y, char ch, COLORREF c);
static void draw_text8x16(int x, int y, const char *s, COLORREF c);
static void draw_footer_one(SortTask *task);

static unsigned char font_digit[10][16] = {
    {0x00,0x3C,0x66,0x6E,0x76,0x66,0x66,0x66,0x66,0x66,0x66,0x76,0x6E,0x66,0x3C,0x00}, /* 0 */
    {0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00}, /* 1 */
    {0x00,0x3C,0x66,0x66,0x06,0x0C,0x18,0x30,0x60,0x60,0x60,0x60,0x66,0x66,0x7E,0x00}, /* 2 */
    {0x00,0x3C,0x66,0x66,0x06,0x06,0x1C,0x1C,0x06,0x06,0x06,0x06,0x66,0x66,0x3C,0x00}, /* 3 */
    {0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0x8C,0xFE,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 4 */
    {0x00,0x7E,0x60,0x60,0x60,0x60,0x7C,0x66,0x06,0x06,0x06,0x06,0x66,0x66,0x3C,0x00}, /* 5 */
    {0x00,0x1C,0x30,0x60,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, /* 6 */
    {0x00,0x7E,0x66,0x06,0x06,0x0C,0x0C,0x18,0x18,0x18,0x30,0x30,0x30,0x30,0x30,0x00}, /* 7 */
    {0x00,0x3C,0x66,0x66,0x66,0x66,0x3C,0x3C,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, /* 8 */
    {0x00,0x3C,0x66,0x66,0x66,0x66,0x66,0x3E,0x06,0x06,0x06,0x06,0x0C,0x18,0x38,0x00}  /* 9 */
};

static unsigned char font_dash[16]  = {0,0,0,0,0,0,0x7E,0x7E,0,0,0,0,0,0,0,0};
static unsigned char font_colon[16] = {0,0,0,0x18,0x18,0,0,0,0,0x18,0x18,0,0,0,0,0};
static unsigned char font_dot[16]   = {0,0,0,0,0,0,0,0,0,0,0,0,0x18,0x18,0,0};
static unsigned char font_space[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};


void sort_thread(void *pv);

static COLORREF rainbow_color(int i)
{
    static COLORREF colors[7] = {
        RGB(255, 0, 0),
        RGB(255, 127, 0),
        RGB(255, 255, 0),
        RGB(0, 255, 0),
        RGB(0, 0, 255),
        RGB(75, 0, 130),
        RGB(148, 0, 211)
    };
    return colors[i % 7];
}

static void swap_elem(Elem *a, Elem *b)
{
    Elem tmp;
    tmp = *a;
    *a = *b;
    *b = tmp;
}

static void copy_array(Elem *dst, Elem *src, int n)
{
    int i;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
}

static void shuffle(Elem *a, int n)
{
    int i, j;
    for (i = n - 1; i > 0; i--) {
        j = rand() % (i + 1);
        swap_elem(&a[i], &a[j]);
    }
}

static void generate_base_array(Elem *base, int n)
{
    int i;

    for (i = 0; i < EQUAL_COUNT; i++) {
        base[i].value = 50;
        base[i].origin_id = i;
        base[i].color = rainbow_color(i);
    }

    for (i = EQUAL_COUNT; i < n; i++) {
        base[i].value = rand() % 100 + 1;
        base[i].origin_id = i;
        base[i].color = RGB(180, 180, 180);
    }

    shuffle(base, n);
}

static void small_delay(void)
{
    volatile int k;
    for (k = 0; k < 20000; k++) ;
}

static void clear_rect(int l, int t, int r, int b, COLORREF c)
{
    int x, y;
    for (y = t; y <= b; y++) {
        for (x = l; x <= r; x++) {
            setPixel(x, y, c);
        }
    }
}

static void draw_border(int l, int t, int r, int b, COLORREF c)
{
    line(l, t, r, t, c);
    line(l, b, r, b, c);
    line(l, t, l, b, c);
    line(r, t, r, b, c);
}

static void draw_region(SortTask *task)
{
    int width, height;
    int i, x, bar_h;
    int usable_w;

    clear_rect(task->left, task->top, task->right, task->bottom, RGB(0, 0, 0));
    draw_border(task->left, task->top, task->right, task->bottom, RGB(255, 255, 255));

    width = task->right - task->left + 1;
    height = task->bottom - task->top + 1;
    usable_w = width - 2;

    for (i = 0; i < task->n; i++) {
        x = task->left + 1 + i * usable_w / task->n;
        bar_h = (task->arr[i].value * (height - 4)) / 100;
        line(x, task->bottom - 1, x, task->bottom - 1 - bar_h, task->arr[i].color);
    }
}
//四个排序
static void bubble_sort_visual(SortTask *task)
{
    int i, j, n;
    n = task->n;

    for (i = 0; i < n - 1; i++) {
        for (j = 0; j < n - 1 - i; j++) {
            if (task->arr[j].value > task->arr[j + 1].value) {
                swap_elem(&task->arr[j], &task->arr[j + 1]);
                draw_region(task);
                small_delay();
            }
        }
    }
}

static void insertion_sort_visual(SortTask *task)
{
    int i, j;
    Elem key;

    for (i = 1; i < task->n; i++) {
        key = task->arr[i];
        j = i - 1;

        while (j >= 0 && task->arr[j].value > key.value) {
            task->arr[j + 1] = task->arr[j];
            j--;
            draw_region(task);
            small_delay();
        }

        task->arr[j + 1] = key;
        draw_region(task);
        small_delay();
    }
}

static int partition_visual(SortTask *task, int l, int r)
{
    int i, j;
    Elem pivot;

    pivot = task->arr[r];
    i = l - 1;

    for (j = l; j < r; j++) {
        if (task->arr[j].value <= pivot.value) {
            i++;
            swap_elem(&task->arr[i], &task->arr[j]);
            draw_region(task);
            small_delay();
        }
    }

    swap_elem(&task->arr[i + 1], &task->arr[r]);
    draw_region(task);
    small_delay();

    return i + 1;
}

static void quick_sort_visual(SortTask *task, int l, int r)
{
    int p;
    if (l < r) {
        p = partition_visual(task, l, r);
        quick_sort_visual(task, l, p - 1);
        quick_sort_visual(task, p + 1, r);
    }
}

static void heapify_visual(SortTask *task, int n, int i)
{
    int largest, l, r;

    largest = i;
    l = 2 * i + 1;
    r = 2 * i + 2;

    if (l < n && task->arr[l].value > task->arr[largest].value)
        largest = l;
    if (r < n && task->arr[r].value > task->arr[largest].value)
        largest = r;

    if (largest != i) {
        swap_elem(&task->arr[i], &task->arr[largest]);
        draw_region(task);
        small_delay();
        heapify_visual(task, n, largest);
    }
}

static void heap_sort_visual(SortTask *task)
{
    int i, n;
    n = task->n;

    for (i = n / 2 - 1; i >= 0; i--)
        heapify_visual(task, n, i);

    for (i = n - 1; i > 0; i--) {
        swap_elem(&task->arr[0], &task->arr[i]);
        draw_region(task);
        small_delay();
        heapify_visual(task, i, 0);
    }
}

static int days_of_month(int year, int month)
{
    static int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

    if (month == 2) {
        if (is_leap(year))
            return 29;
        else
            return 28;
    }

    return mdays[month - 1];
}

static void unix_to_datetime(time_t sec,
                             int *year, int *month, int *day,
                             int *hour, int *minute, int *second)
{
    long days;
    long sec_of_day;
    int y, m;

    sec = sec + 8 * 3600;

    days = sec / 86400;
    sec_of_day = sec % 86400;

    *hour = sec_of_day / 3600;
    *minute = (sec_of_day % 3600) / 60;
    *second = sec_of_day % 60;

    y = 1970;
    while (1) {
        int ydays = is_leap(y) ? 366 : 365;
        if (days >= ydays) {
            days -= ydays;
            y++;
        } else {
            break;
        }
    }

    m = 1;
    while (days >= days_of_month(y, m)) {
        days -= days_of_month(y, m);
        m++;
    }

    *year = y;
    *month = m;
    *day = (int)days + 1;
}

static void format_precise_time(time_t sec, unsigned int msec, char *buf)
{
    int year, month, day;
    int hour, minute, second;

    unix_to_datetime(sec, &year, &month, &day, &hour, &minute, &second);

    sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d.%03u",
            year, month, day, hour, minute, second, msec);
}

static void draw_char8x16(int x, int y, char ch, COLORREF c)
{
    unsigned char *glyph;
    int row, col;

    if (ch >= '0' && ch <= '9')
        glyph = font_digit[ch - '0'];
    else if (ch == '-')
        glyph = font_dash;
    else if (ch == ':')
        glyph = font_colon;
    else if (ch == '.')
        glyph = font_dot;
    else if (ch == ' ')
        glyph = font_space;
    else
        glyph = font_space;

    for (row = 0; row < 16; row++) {
        for (col = 0; col < 8; col++) {
            if (glyph[row] & (1 << (7 - col))) {
                setPixel(x + col, y + row, c);
            }
        }
    }
}

static void draw_text8x16(int x, int y, const char *s, COLORREF c)
{
    while (*s) {
        draw_char8x16(x, y, *s, c);
        x += 8;
        s++;
    }
}

static void draw_footer_one(SortTask *task)
{
    int x, y;

    x = task->left + 4;
    y = task->footer_top + 4;

    clear_rect(task->left, task->footer_top,
               task->right, task->footer_bottom,
               RGB(0, 0, 0));

    draw_border(task->left, task->footer_top,
                task->right, task->footer_bottom,
                RGB(255, 255, 255));

    draw_text8x16(x, y, task->start_text, RGB(0,255,0));
    draw_text8x16(x, y + 20, task->end_text, RGB(255,255,0));
}

void sort_thread(void *pv)
{
    SortTask *task;
    task = (SortTask *)pv;

    time_precise(&task->start_sec, &task->start_msec);
    format_precise_time(task->start_sec, task->start_msec, task->start_text);

    draw_region(task);
    draw_footer_one(task);

    switch (task->algo_type) {
    case 0:
        bubble_sort_visual(task);
        break;
    case 1:
        insertion_sort_visual(task);
        break;
    case 2:
        quick_sort_visual(task, 0, task->n - 1);
        break;
    case 3:
        heap_sort_visual(task);
        break;
    }

    time_precise(&task->end_sec, &task->end_msec);
    format_precise_time(task->end_sec, task->end_msec, task->end_text);

    task->done = 1;
    draw_region(task);
    draw_footer_one(task);

    task_exit(0);
}

void __main()
{
    size_t heap_size = 32*1024*1024;
    void  *heap_base = mmap(NULL, heap_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
	g_heap = tlsf_create_with_pool(heap_base, heap_size);
}

/**
 * 第一个运行在用户模式的线程所执行的函数
 */
int is_leap(int year)
{
    if ((year % 400) == 0) return 1;
    if ((year % 100) == 0) return 0;
    if ((year % 4) == 0) return 1;
    return 0;
}

void print_chongqing_time(time_t t)
{
    long days;
    long sec_of_day;
    int year = 1970;
    int month = 0;
    int day;
    int hour, minute, second;
    int mdays[12];

    
    t = t + 8 * 3600;

    days = t / 86400;
    sec_of_day = t % 86400;

    hour = sec_of_day / 3600;
    minute = (sec_of_day % 3600) / 60;
    second = sec_of_day % 60;

    while (1) {
        int ydays = is_leap(year) ? 366 : 365;
        if (days >= ydays) {
            days -= ydays;
            year++;
        } else {
            break;
        }
    }

    mdays[0] = 31;
    mdays[1] = is_leap(year) ? 29 : 28;
    mdays[2] = 31;
    mdays[3] = 30;
    mdays[4] = 31;
    mdays[5] = 30;
    mdays[6] = 31;
    mdays[7] = 31;
    mdays[8] = 30;
    mdays[9] = 31;
    mdays[10] = 30;
    mdays[11] = 31;

    while (days >= mdays[month]) {
        days -= mdays[month];
        month++;
    }

    day = (int)days + 1;

    printf("local_time: %04d-%02d-%02d %02d:%02d:%02d\n",
           year, month + 1, day, hour, minute, second);
}
void main(void *pv)
{
    //声明变量
    Elem *base;
    SortTask tasks[THREAD_COUNT];
    int screen_w, screen_h;
    int sort_h, col_w;
    int i, code;

    //生成随机数组
    srand(time(NULL));

    base = (Elem *)malloc(sizeof(Elem) * SORT_N);
    generate_base_array(base, SORT_N);

    //进入图形模式
    init_graphic(0x143);

    screen_w = g_graphic_dev.XResolution;
    screen_h = g_graphic_dev.YResolution;
    sort_h = screen_h - FOOTER_HEIGHT;
    col_w = screen_w / THREAD_COUNT;

    //初始化四个结构任务体
    for (i = 0; i < THREAD_COUNT; i++) {
        tasks[i].arr = (Elem *)malloc(sizeof(Elem) * SORT_N);
        copy_array(tasks[i].arr, base, SORT_N);

        tasks[i].stack = (unsigned char *)malloc(STACK_SIZE);
        tasks[i].n = SORT_N;
        tasks[i].left = i * col_w;
        tasks[i].right = (i + 1) * col_w - 1;
        tasks[i].top = 0;
        tasks[i].bottom = sort_h - 1;
        tasks[i].footer_top = sort_h;
        tasks[i].footer_bottom = screen_h - 1;
        tasks[i].done = 0;
    }

    strcpy(tasks[0].name, "Bubble");
    tasks[0].algo_type = 0;

    strcpy(tasks[1].name, "Insert");
    tasks[1].algo_type = 1;

    strcpy(tasks[2].name, "Quick");
    tasks[2].algo_type = 2;

    strcpy(tasks[3].name, "Heap");
    tasks[3].algo_type = 3;

    printf("task #%d: I'm the first user task(pv=0x%08x)!\r\n",
            task_getid(), pv);

    for (i = 0; i < THREAD_COUNT; i++) {
        draw_region(&tasks[i]);
    }

    for (i = 0; i < THREAD_COUNT; i++) {
        tasks[i].tid = task_create(tasks[i].stack + STACK_SIZE,
                                   &sort_thread,
                                   &tasks[i]);
    }

    for (i = 0; i < THREAD_COUNT; i++) {
        task_wait(tasks[i].tid, &code);
    }

    for (i = 0; i < THREAD_COUNT; i++) {
        free(tasks[i].stack);
        free(tasks[i].arr);
    }

    free(base);

    while (1)
    ;

    // //TODO: Your code goes here
    // printf("Hello Operating System!");
    // time_t t1, t2;
    // t1 = time(&t2);

    // printf("t1 = %ld\n", t1);
    // printf("t2 = %ld\n", t2);

    // if (t1 == t2)
    //     printf("OK: t1 == t2\n");
    // else
    //     printf("ERROR: t1 != t2\n");

    // print_chongqing_time(t1);
    
    // while(1)
    //     ;
    // task_exit(0);
}

