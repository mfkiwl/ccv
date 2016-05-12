#include <ccv.h>
#include <ccv_internal.h>
#include <nnc/ccv_nnc.h>
#include <nnc/ccv_nnc_easy.h>
#include <3rdparty/dsfmt/dSFMT.h>
#include <sys/time.h>
#include <ctype.h>

static unsigned int get_current_time(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

#define INPUT_DIM (512)
#define OUTPUT_DIM (512)

#define INPUT_SIZE (55)
#define OUTPUT_SIZE (55)

#define KERNEL_SIZE (3)

int main(int argc, char** argv)
{
	ccv_nnc_init();
	ccv_nnc_tensor_t* a = ccv_nnc_tensor_new(0, ONE_CPU_TENSOR(INPUT_DIM, INPUT_SIZE, INPUT_SIZE, 64), 0);
	ccv_nnc_tensor_t* b = ccv_nnc_tensor_new(0, ONE_CPU_TENSOR(OUTPUT_DIM, OUTPUT_SIZE, OUTPUT_SIZE, 64), 0);
	ccv_nnc_cmd_t cmd = ccv_nnc_cmd(CCV_NNC_COMPUTE_CONVOLUTIONAL_FORWARD, 0, CMD_CONVOLUTIONAL(OUTPUT_DIM, INPUT_DIM, KERNEL_SIZE, KERNEL_SIZE), 0);
	ccv_nnc_hint_t hint = ccv_nnc_hint_guess(cmd.info, &a->info, 1, &b->info, 1);
	assert(ccv_nnc_hint_verify(hint, cmd.info, a->info, b->info) == 0);
	ccv_nnc_tensor_t* w = ccv_nnc_tensor_new(0, ONE_CPU_TENSOR(INPUT_DIM, KERNEL_SIZE, KERNEL_SIZE, OUTPUT_DIM), 0);
	ccv_nnc_tensor_t* bias = ccv_nnc_tensor_new(0, ONE_CPU_TENSOR(OUTPUT_DIM), 0);
	// configure the inlets.
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	for (i = 0; i < INPUT_DIM * KERNEL_SIZE * KERNEL_SIZE * OUTPUT_DIM; i++)
		w->data.f32[i] = dsfmt_genrand_open_close(&dsfmt) / (INPUT_DIM * KERNEL_SIZE * KERNEL_SIZE);
	for (i = 0; i < INPUT_SIZE * INPUT_SIZE * INPUT_DIM * 64; i++)
		a->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < OUTPUT_DIM; i++)
		bias->data.f32[i] = (float)i / OUTPUT_DIM;
	// Copy generated matrix values over to GPU.
	ccv_nnc_tensor_t* ga = ccv_nnc_tensor_new(0, ONE_GPU_TENSOR(00, INPUT_DIM, INPUT_SIZE, INPUT_SIZE, 64), 0);
	ccv_nnc_tensor_t* gw = ccv_nnc_tensor_new(0, ONE_GPU_TENSOR(00, INPUT_DIM, KERNEL_SIZE, KERNEL_SIZE, OUTPUT_DIM), 0);
	ccv_nnc_tensor_t* gbias = ccv_nnc_tensor_new(0, ONE_GPU_TENSOR(00, OUTPUT_DIM), 0);
	unsigned int elapsed_time = get_current_time();
	ccv_nnc_cmd_t move = ccv_nnc_cmd(CCV_NNC_COMPUTE_DATA_TRANSFER, 0, ccv_nnc_default_cmd_params, 0);
	move.backend = 3; // CCV_NNC_BACKEND_GPU_REF = 3
	ccv_nnc_cmd_exec(move, ccv_nnc_default_hint, 0, TENSOR_LIST(a, w, bias), TENSOR_LIST(ga, gw, gbias), 0);
	ccv_nnc_cmd_exec(cmd, hint, 0, TENSOR_LIST(a, w, bias), TENSOR_LIST(b), 0);
	elapsed_time = get_current_time() - elapsed_time;
	printf("%u ms for ref\n", elapsed_time);
	ga->info.format = CCV_TENSOR_FORMAT_NCHW;
	ga->info.dim[0] = INPUT_SIZE;
	ga->info.dim[1] = INPUT_SIZE;
	ga->info.dim[2] = INPUT_DIM;
	ga->info.dim[3] = 64;
	ccv_nnc_tensor_t* gc = ccv_nnc_tensor_new(0, ONE_GPU_TENSOR(00, OUTPUT_DIM, OUTPUT_SIZE, OUTPUT_SIZE, 64), 0);
	gc->info.format = CCV_TENSOR_FORMAT_NCHW;
	gc->info.dim[0] = OUTPUT_SIZE;
	gc->info.dim[1] = OUTPUT_SIZE;
	gc->info.dim[2] = OUTPUT_DIM;
	gc->info.dim[3] = 64;
	cmd.backend = 2; // CCV_NNC_BACKEND_GPU_CUDNN = 0
	cmd.algorithm = -1;
	ccv_nnc_stream_context_t* stream_context = ccv_nnc_stream_context_new(CCV_STREAM_CONTEXT_GPU);
	cmd = ccv_nnc_cmd_autotune(cmd, 0, hint, 0, TENSOR_LIST(ga, gw, gbias), TENSOR_LIST(gc), stream_context);
	elapsed_time = get_current_time();
	assert(CCV_NNC_EXEC_SUCCESS == ccv_nnc_cmd_exec(cmd, hint, 0, TENSOR_LIST(ga, gw, gbias), TENSOR_LIST(gc), stream_context));
	ccv_nnc_stream_context_wait(stream_context);
	ccv_nnc_stream_context_free(stream_context);
	elapsed_time = get_current_time() - elapsed_time;
	printf("%u ms for optimized\n", elapsed_time);
	ccv_nnc_tensor_t* c = ccv_nnc_tensor_new(0, ONE_CPU_TENSOR(OUTPUT_DIM, OUTPUT_SIZE, OUTPUT_SIZE, 64), 0);
	ccv_nnc_cmd_exec(move, ccv_nnc_default_hint, 0, TENSOR_LIST(gc), TENSOR_LIST(c), 0);
	//for (i = 0; i < OUTPUT_DIM * OUTPUT_SIZE * OUTPUT_SIZE; i++)
	//	if (fabs(b->data.f32[i] - c->data.f32[i]) > 1e-5)
	//		printf("%d %f %f\n", i, b->data.f32[i], c->data.f32[i]);
	ccv_nnc_tensor_free(c);
	ccv_nnc_tensor_free(gc);
	ccv_nnc_tensor_free(bias);
	ccv_nnc_tensor_free(w);
	ccv_nnc_tensor_free(b);
	ccv_nnc_tensor_free(a);
	ccv_nnc_tensor_free(gbias);
	ccv_nnc_tensor_free(gw);
	ccv_nnc_tensor_free(ga);
}