#include "Smoother.h"

Smoother::Smoother(int size, int correctionOffset, int min, int max)
  : bufferSize(size),
    bufferIndex(0),
    correctionOffset(correctionOffset),
    minLimit(min),
    maxLimit(max)
{
  buffer = new int[bufferSize];
  fillBufferOnStart(0);
}

Smoother::~Smoother() {
  delete[] buffer;
}

void Smoother::fillBufferOnStart(int initialValue) {
  for (int i = 0; i < bufferSize; ++i) {
    buffer[i] = initialValue + correctionOffset;
  }
  bufferIndex = 0;
}

void Smoother::reset() {
  fillBufferOnStart(0);
}

void Smoother::setCorrectionOffset(int offset) {
  correctionOffset = offset;
}

void Smoother::setLimits(int min, int max) {
  minLimit = min;
  maxLimit = max;
}

int Smoother::applyLimiter(int value) {
  if (value > maxLimit) return maxLimit;
  if (value < minLimit) return minLimit;
  return value;
}

int Smoother::smooth(int newValue) {
  buffer[bufferIndex] = newValue + correctionOffset;
  bufferIndex = (bufferIndex + 1) % bufferSize;

  long sum = 0;
  for (int i = 0; i < bufferSize; ++i) {
    sum += buffer[i];
  }

  int average = sum / bufferSize;
  return applyLimiter(average);
}
