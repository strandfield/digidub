// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#ifndef PHASH_H
#define PHASH_H

#include <QtGlobal>

#include <bit>
#include <vector>

class QImage;

class PerceptualHash
{
public:
  PerceptualHash();

  quint64 hash(const QImage& image);
  quint64 hash(const QString& filePath);

  bool checkImage(const QImage& image, bool* sizeOk = nullptr, bool* grayscaleOk = nullptr);

private:
  std::vector<double> m_dct0;
  std::vector<double> m_dct1;
  std::vector<double> m_lowfreqs;
  std::vector<bool> m_hashbits;
};

int phashDist(int a, int b) = delete;
inline int phashDist(quint64 a, quint64 b)
{
  return std::popcount(a ^ b);
}

quint64 computeHash(const QImage& image);

#endif // PHASH_H
