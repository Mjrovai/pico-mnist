/*** Include ***/
#include <cstdint>
#include <cstdio>
// #include <cstdlib>
#include <cstring>

#include "pico/stdlib.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"

#include "conv_mnist_quant.h"
#include "LcdIli9341SPI.h"
#include "TpTsc2046SPI.h"

/*** Const ***/
static constexpr std::array<uint8_t, 2> COLOR_BG   = { 0x00, 0x1F };
static constexpr std::array<uint8_t, 2> COLOR_AREA = { 0xF0, 0x0F };
static constexpr std::array<uint8_t, 2> COLOR_LINE = { 0x07, 0xE0 };
static constexpr int32_t AREA_X0 = 100;
static constexpr int32_t AREA_Y0 =  50;
static constexpr int32_t AREA_X1 = 100 + 100;
static constexpr int32_t AREA_Y1 =  50 + 100;
static constexpr int32_t MNIST_W =  28;
static constexpr int32_t MNIST_H =  28;

/*** Macro ***/
#define HALT() do{while(1) sleep_ms(100);}while(0)

/*** Static Variable ***/
static int8_t s_mnistBuffer[MNIST_W * MNIST_H];

/*** Function ***/
static tflite::MicroInterpreter* createStaticInterpreter(void);
static LcdIli9341SPI* createStaticLcd(void);
static TpTsc2046SPI* createStaticTp(void);
static void writeMnistBuffer(float tpX, float tpY);
static void reset(LcdIli9341SPI* lcd);
static void run(LcdIli9341SPI* lcd, tflite::MicroInterpreter* interpreter);

int main() {
	stdio_init_all();
	sleep_ms(1000);		// wait until UART connected
	printf("Hello, world!\n");

	/* Create interpreter */
	tflite::MicroInterpreter* interpreter = createStaticInterpreter();
	if (!interpreter) {
		printf("createStaticInterpreter failed\n");
		HALT();
	}

	/* Create sub modules for paint */
	LcdIli9341SPI* lcd = createStaticLcd();
	TpTsc2046SPI* tp = createStaticTp();
	reset(lcd);

	int32_t tpXprevious = -1;
	int32_t tpYprevious = -1;
	while(1) {
		float tpX, tpY, tpPressure;
		tp->getFromDevice(tpX, tpY, tpPressure);
		if (tpPressure > 50 && tpX < 0.95) {
			// printf("%.03f %.03f %.1f\n", tpX, tpY, tpPressure);
			tpX *= LcdIli9341SPI::WIDTH;
			tpY *= LcdIli9341SPI::HEIGHT;
			if (tpXprevious != -1) {
				if(std::abs(tpX - tpXprevious) < 10 && std::abs(tpY - tpYprevious) < 10) {	// to reduce noise
					lcd->drawLine(tpXprevious, tpYprevious, tpX, tpY, 2, COLOR_LINE);
					writeMnistBuffer(tpX, tpY);
				}
			} else if (LcdIli9341SPI::WIDTH - 80 < tpX && tpY < 50) {
				reset(lcd);
			} else if (LcdIli9341SPI::WIDTH - 80 < tpX && LcdIli9341SPI::HEIGHT - 50 < tpY) {
				run(lcd, interpreter);
			}
			tpXprevious = tpX;
			tpYprevious = tpY;			
		} else {
			tpXprevious = -1;
			tpYprevious = -1;
		}
	}

	lcd->finalize();
	tp->finalize();
	HALT();
	return 0;
}

static tflite::MicroInterpreter* createStaticInterpreter(void)
{
	constexpr int kTensorArenaSize = 10000;
	static uint8_t tensor_arena[kTensorArenaSize];
	static tflite::MicroErrorReporter micro_error_reporter;
	static tflite::ErrorReporter* error_reporter = &micro_error_reporter;

	const tflite::Model* model = tflite::GetModel(conv_mnist_quant_tflite);
	if (model->version() != TFLITE_SCHEMA_VERSION) {
		TF_LITE_REPORT_ERROR(error_reporter,
			"Model provided is schema version %d not equal "
			"to supported version %d.",
			model->version(), TFLITE_SCHEMA_VERSION);
			return nullptr;
	}

	static tflite::AllOpsResolver resolver;
	static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena, kTensorArenaSize, error_reporter);
	tflite::MicroInterpreter* interpreter = &static_interpreter;
	TfLiteStatus allocate_status = interpreter->AllocateTensors();
	if (allocate_status != kTfLiteOk) {
		TF_LITE_REPORT_ERROR(error_reporter, "AllocateTensors() failed");
		return nullptr;
	}
	return interpreter;
}


static LcdIli9341SPI* createStaticLcd(void)
{
	static LcdIli9341SPI lcd;
	LcdIli9341SPI::CONFIG lcdConfig;
	lcdConfig.spiPortNum = 0;
	lcdConfig.pinSck = 2;
	lcdConfig.pinMosi = 3;
	lcdConfig.pinMiso = 4;
	lcdConfig.pinCs = 5;
	lcdConfig.pinDc = 7;
	lcdConfig.pinReset = 6;
	lcd.initialize(lcdConfig);
	lcd.test();
	return &lcd;
}

static TpTsc2046SPI* createStaticTp(void)
{
	static TpTsc2046SPI tp;
	TpTsc2046SPI::CONFIG tpConfig;
	tpConfig.spiPortNum = 1;
	tpConfig.pinSck = 10;
	tpConfig.pinMosi = 11;
	tpConfig.pinMiso = 12;
	tpConfig.pinCs = 13;
	tpConfig.pinIrq = 14;
	tpConfig.callback = nullptr;
	tp.initialize(tpConfig);
	return &tp;
}

static void writeMnistBuffer(float tpX, float tpY)
{
	float x = (tpX - AREA_X0) / (AREA_X1 - AREA_X0);
	float y = (tpY - AREA_Y0) / (AREA_Y1 - AREA_Y0);
	int32_t xInBuff = static_cast<int32_t>(x * MNIST_W);
	int32_t yInBuff = static_cast<int32_t>(y * MNIST_H);
	if (xInBuff < 0 || MNIST_W <= xInBuff) return;
	if (yInBuff < 0 || MNIST_H <= yInBuff) return;
	s_mnistBuffer[xInBuff + yInBuff * MNIST_W] = 1;
}

static void reset(LcdIli9341SPI* lcd)
{
	printf("reset\n");
	lcd->drawRect(0, 0, LcdIli9341SPI::WIDTH, LcdIli9341SPI::HEIGHT, COLOR_BG);
	lcd->drawRect(AREA_X0, AREA_Y0, AREA_X1 - AREA_X0, AREA_Y1 - AREA_Y0, COLOR_AREA);
	lcd->setCharPos(LcdIli9341SPI::WIDTH - 100, 10);
	lcd->putText("CLEAR");
	lcd->setCharPos(LcdIli9341SPI::WIDTH - 50, LcdIli9341SPI::HEIGHT - 50);
	lcd->putText("RUN");

	memset(s_mnistBuffer, 0, sizeof(s_mnistBuffer));
}

static void run(LcdIli9341SPI* lcd, tflite::MicroInterpreter* interpreter)
{
	/* Debug display */
	printf("run\n");
	for (int32_t y = 0; y < MNIST_H; y++) {
		for (int32_t x = 0; x < MNIST_W; x++) {
			printf("%d", s_mnistBuffer[x + y * MNIST_W]);
		}
		printf("\n");
	}

	/* Set input data as int8 (-128 - 127) */
	TfLiteTensor* input = interpreter->input(0);
	TfLiteTensor* output = interpreter->output(0);
	for(int i = 0; i < MNIST_W * MNIST_H; i++) {
		if (s_mnistBuffer[i] == 1) {
			input->data.int8[i] = 127;
		} else {
			input->data.int8[i] = -128;
		}
	}

	/* Inference */
	TfLiteStatus invoke_status = interpreter->Invoke();
	if (invoke_status != kTfLiteOk) {
		printf("Invoke failed\n");
		HALT();
	}

	/* Show result */
	int32_t maxIndex = 0;
	float maxScore = 0;
	for(int32_t i = 0; i < 10; i++) {;
		float score = (output->data.int8[i] - output->params.zero_point) * output->params.scale;
		char text[10];
		snprintf(text, sizeof(text), "%d:%.2f", i, score);
		printf("%s\n", text);;
		lcd->setCharPos(5, i * 24);
		lcd->putText(text);

		if (score > maxScore) {
			maxScore = score;
			maxIndex = i;
		}
	}
	char text[10];
	snprintf(text, sizeof(text), "* %d *", maxIndex);
	lcd->setCharPos((AREA_X0 + AREA_X1) / 2, AREA_Y1 + 10);
	lcd->putText(text);
}
