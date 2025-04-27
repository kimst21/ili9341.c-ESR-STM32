#include <stddef.h>                  // 표준 자료형 정의를 포함하는 헤더
#include "stm32f4xx_hal.h"            // STM32F4 시리즈용 HAL 드라이버 헤더
#include "ili9341.h"                  // ILI9341 LCD 제어용 헤더 파일

#define DMA_BUFFER_SIZE 64U           // DMA 전송 버퍼 크기 (64개 픽셀 단위)

extern SPI_HandleTypeDef hspi4;        // SPI4 주변장치 핸들 (LCD 통신용)
extern DMA_HandleTypeDef hdma_spi4_tx; // SPI4 송신용 DMA 핸들

static volatile bool txComplete;       // DMA 전송 완료 여부를 나타내는 플래그

// 내부에서만 사용하는 함수 선언
static void SetWindow(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd);
static void WriteDataDMA(const void *data, uint16_t length);
static void WaitForDMAWriteComplete(void);
static void WriteCommand(uint8_t command);
static void WriteData(uint8_t data);

// 명령어를 LCD에 전송하는 함수
static void WriteCommand(uint8_t command)
{
	HAL_GPIO_WritePin(ILI9341_DC_PORT, ILI9341_DC_PIN, GPIO_PIN_RESET); // DC 핀을 명령어 모드로 설정
	HAL_SPI_Transmit(&hspi4, &command, 1U, 100U);                       // SPI로 1바이트 명령어 전송
}

// 데이터(픽셀 정보 등)를 LCD에 전송하는 함수
static void WriteData(uint8_t data)
{
	HAL_GPIO_WritePin(ILI9341_DC_PORT, ILI9341_DC_PIN, GPIO_PIN_SET);   // DC 핀을 데이터 모드로 설정
	HAL_SPI_Transmit(&hspi4, &data, 1U, 100U);                          // SPI로 1바이트 데이터 전송
}

// SPI DMA 전송 완료 시 호출되는 콜백 함수
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
	txComplete = true;   // 전송 완료 플래그를 참(true)로 설정
}

// ILI9341 디스플레이를 리셋하는 함수
void ILI9341Reset(void)
{
	HAL_GPIO_WritePin(ILI9341_RST_PORT, ILI9341_RST_PIN, GPIO_PIN_RESET); // 리셋 핀을 낮춤(리셋 시작)
	HAL_Delay(200UL);                                                     // 200밀리초 대기
	HAL_GPIO_WritePin(ILI9341_RST_PORT, ILI9341_RST_PIN, GPIO_PIN_SET);    // 리셋 핀을 올림(리셋 해제)
	HAL_Delay(200UL);                                                     // 200밀리초 대기
}

// ILI9341 디스플레이를 초기화하는 함수
void ILI9341Init(void)
{
	HAL_GPIO_WritePin(ILI9341_CS_PORT, ILI9341_CS_PIN, GPIO_PIN_RESET); // 디스플레이 선택 (CS 핀 활성화)

	// 디스플레이 설정을 위한 초기화 명령어 시퀀스 전송
	WriteCommand(0x01U); // 소프트웨어 리셋
	HAL_Delay(1000UL);   // 1초 대기

	// 전원 설정 및 감마 설정 등
	WriteCommand(0xCBU);
	WriteData(0x39U); WriteData(0x2CU); WriteData(0x00U); WriteData(0x34U); WriteData(0x02U);

	WriteCommand(0xCFU);
	WriteData(0x00U); WriteData(0xC1U); WriteData(0x30U);

	WriteCommand(0xE8U);
	WriteData(0x85U); WriteData(0x00U); WriteData(0x78U);

	WriteCommand(0xEAU);
	WriteData(0x00U); WriteData(0x00U);

	WriteCommand(0xEDU);
	WriteData(0x64U); WriteData(0x03U); WriteData(0x12U); WriteData(0x81U);

	WriteCommand(0xF7U);
	WriteData(0x20U);

	WriteCommand(0xC0U); // 전원 제어 1
	WriteData(0x23U);

	WriteCommand(0xC1U); // 전원 제어 2
	WriteData(0x10U);

	WriteCommand(0xC5U); // VCOM 제어 1
	WriteData(0x3EU); WriteData(0x28U);

	WriteCommand(0xC7U); // VCOM 제어 2
	WriteData(0x86U);

	WriteCommand(0x36U); // 메모리 접근 제어
	WriteData(0x48U);

	WriteCommand(0x3AU); // 픽셀 포맷 설정
	WriteData(0x55U);    // 16비트(RGB565) 설정

	WriteCommand(0xB1U); // 프레임 레이트 제어
	WriteData(0x00U); WriteData(0x18U);

	WriteCommand(0xB6U); // 디스플레이 기능 제어
	WriteData(0x08U); WriteData(0x82U); WriteData(0x27U);

	WriteCommand(0xF2U); // 감마 함수 비활성화
	WriteData(0x00U);

	WriteCommand(0x26U); // 감마 곡선 선택
	WriteData(0x01U);

	// 양의 감마 설정
	WriteCommand(0xE0U);
	WriteData(0x0FU); WriteData(0x31U); WriteData(0x2BU); WriteData(0x0CU);
	WriteData(0x0EU); WriteData(0x08U); WriteData(0x4EU); WriteData(0xF1U);
	WriteData(0x37U); WriteData(0x07U); WriteData(0x10U); WriteData(0x03U);
	WriteData(0x0EU); WriteData(0x09U); WriteData(0x00U);

	// 음의 감마 설정
	WriteCommand(0xE1U);
	WriteData(0x00U); WriteData(0x0EU); WriteData(0x14U); WriteData(0x03U);
	WriteData(0x11U); WriteData(0x07U); WriteData(0x31U); WriteData(0xC1U);
	WriteData(0x48U); WriteData(0x08U); WriteData(0x0FU); WriteData(0x0CU);
	WriteData(0x31U); WriteData(0x36U); WriteData(0x0FU);

	WriteCommand(0x11U); // 수면 모드 해제
	HAL_Delay(120UL);    // 120밀리초 대기

	WriteCommand(0x29U); // 화면 켜기
	WriteCommand(0x36U);
	WriteData(0x48U);    // 화면 방향 설정
}

// 디스플레이에 출력할 영역 설정 함수
static void SetWindow(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd)
{
	WriteCommand(0x2AU); // 컬럼 주소 설정
	WriteData(xStart >> 8);
	WriteData(xStart);
	WriteData(xEnd >> 8);
	WriteData(xEnd);

	WriteCommand(0x2BU); // 페이지 주소 설정
	WriteData(yStart >> 8);
	WriteData(yStart);
	WriteData(yEnd >> 8);
	WriteData(yEnd);

	WriteCommand(0x2CU); // 메모리 쓰기 시작

	HAL_GPIO_WritePin(ILI9341_DC_PORT, ILI9341_DC_PIN, GPIO_PIN_SET); // 이후 전송은 모두 데이터로 처리
}

// 1개의 픽셀을 LCD에 그리는 함수
void ILI9341Pixel(uint16_t x, uint16_t y, colour_t colour)
{
	colour_t beColour = __builtin_bswap16(colour); // 색상 바이트 순서 변경 (빅엔디언 방식)

	if (x >= ILI9341_LCD_WIDTH || y >= ILI9341_LCD_HEIGHT)
	{
		return; // 화면 범위를 벗어난 경우 무시
	}

	SetWindow(x, y, x, y); // 해당 좌표만큼 창 설정

	HAL_SPI_Transmit(&hspi4, (uint8_t *)&beColour, 2U, 100UL); // 픽셀 색상을 SPI로 전송
}

// 컬러 비트맵 이미지를 출력하는 함수
void ILI9341DrawColourBitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *imageData)
{
	uint16_t bytestToWrite;

	SetWindow(x, y, x + width - 1U, y + height - 1U); // 그림을 출력할 창 설정
	bytestToWrite = width * height * 2U;               // 총 전송할 데이터 크기 계산

	WriteDataDMA(imageData, bytestToWrite);            // DMA로 이미지 데이터 전송
	WaitForDMAWriteComplete();                         // 전송 완료 대기
}

// 단색(1비트) 비트맵 이미지를 출력하는 함수
void ILI9341DrawMonoBitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *imageData, colour_t fgColour, colour_t bgColour)
{
	colour_t beFgColour = __builtin_bswap16(fgColour);
	colour_t beBgColour = __builtin_bswap16(bgColour);
	colour_t dmaBuffer[DMA_BUFFER_SIZE];
	uint32_t totalBytesToWrite;
	uint32_t bytesToWriteThisTime;
	uint8_t mask = 0x80U;
	uint16_t pixelsWritten = 0U;
	uint8_t i;

	SetWindow(x, y, x + width - 1U, y + height - 1U);
	totalBytesToWrite = (uint32_t)width * (uint32_t)height * (uint32_t)sizeof(colour_t);
	bytesToWriteThisTime = DMA_BUFFER_SIZE * (uint32_t)sizeof(colour_t);

	while (totalBytesToWrite > 0UL)
	{
		if (totalBytesToWrite < bytesToWriteThisTime)
		{
			bytesToWriteThisTime = totalBytesToWrite;
		}
		totalBytesToWrite -= bytesToWriteThisTime;

		for (i = 0U; i < bytesToWriteThisTime / 2UL; i++)
		{
			if ((mask & *imageData) == 0U)
			{
				dmaBuffer[i] = beFgColour; // 비트가 0이면 전경색 사용
			}
			else
			{
				dmaBuffer[i] = beBgColour; // 비트가 1이면 배경색 사용
			}
			pixelsWritten++;
			mask >>= 1;
			if (mask == 0U)
			{
				mask = 0x80U;
				imageData++;
			}

			if (pixelsWritten % width == 0U && mask != 0x80U)
			{
				mask = 0x80U;
				imageData++;
			}
		}

		WriteDataDMA(&dmaBuffer, bytesToWriteThisTime);
		WaitForDMAWriteComplete();
	}
}

// 사각형 전체를 동일한 색으로 채우는 함수
void ILI9341FilledRectangle(uint16_t x, uint16_t y, uint16_t width, uint16_t height, colour_t colour)
{
	colour_t dmaBuffer[DMA_BUFFER_SIZE];
	uint8_t i;
	uint32_t totalBytesToWrite;
	uint32_t bytesToWriteThisTime;

	for (i = 0U; i < DMA_BUFFER_SIZE; i++)
	{
		dmaBuffer[i] = __builtin_bswap16(colour); // 버퍼 전체를 동일한 색상으로 채움
	}

	SetWindow(x, y, x + width - 1U, y + height - 1U);
	totalBytesToWrite = (uint32_t)width * (uint32_t)height * (uint32_t)sizeof(colour_t);
	bytesToWriteThisTime = DMA_BUFFER_SIZE * (uint16_t)sizeof(colour_t);

	while (totalBytesToWrite > 0UL)
	{
		if (totalBytesToWrite < bytesToWriteThisTime)
		{
			bytesToWriteThisTime = totalBytesToWrite;
		}
		totalBytesToWrite -= bytesToWriteThisTime;

		WriteDataDMA(&dmaBuffer, bytesToWriteThisTime);
		WaitForDMAWriteComplete();
	}
}

// DMA를 사용하여 데이터를 전송하는 함수
static void WriteDataDMA(const void *data, uint16_t length)
{
	txComplete = false;                                                 // 전송 시작 전 완료 플래그 초기화
	HAL_SPI_Transmit_DMA(&hspi4, (uint8_t *)data, length);              // SPI를 통해 DMA 전송 시작
}

// DMA 데이터 전송 완료를 기다리는 함수
static void WaitForDMAWriteComplete(void)
{
	while (txComplete == false) {}                                       // 전송 완료될 때까지 대기
}
