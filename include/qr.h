#pragma once

#define QR_SIZE 37

int qr_encode(const char* text, unsigned char matrix[QR_SIZE][QR_SIZE]);
int qr_self_test(void);
