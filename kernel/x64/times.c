#include "times.h"


// 操作系统当前正在使用的“最佳”时钟
struct clocksource *curr_clocksource = NULL;
struct clock_event_device *curr_clockevent = NULL;

// 注册函数：硬件驱动探测完毕后，调用此函数把自己交上去
void clocksource_register(struct clocksource *new_cs) {
    // 1. 把 new_cs 加入全局链表
    // 2. 比较评分：如果 new_cs->rating > curr_clocksource->rating
    //    内核立刻“移情别恋”，把 curr_clocksource 切换为新的！
}
