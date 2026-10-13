/* Test double, NEVER a deployable model. */
#define IMAI_API_QUEUE
#define IMAI_DATA_IN_COUNT 3
#define IMAI_DATA_OUT_COUNT 6
#define IMAI_RET_SUCCESS 0
#define IMAI_RET_NODATA -1
int IMAI_init(void);
int IMAI_enqueue(const float *input);
int IMAI_dequeue(float *output);
