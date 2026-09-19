#ifndef __APP_WORKQUEUE_H__
#define __APP_WORKQUEUE_H__

typedef void (*work_t)(void *param);

/* 创建通用后台工作队列及其消费者任务。 */
void workqueue_init(void);

/* 投递一个普通任务上下文中执行的工作项；队列满时会阻塞。 */
void workqueue_run(work_t work, void *param);

#endif /* __APP_WORKQUEUE_H__ */
