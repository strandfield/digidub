#include "phash.h"

#include <QImage>

#include <QDebug>

#include <algorithm>
#include <iterator>

// for reference:
// https://docs.scipy.org/doc/scipy/reference/generated/scipy.fftpack.dct.html

namespace {

// axis 0 is matrix line, which would be y-axis for an image.
template<typename PixelData>
void dct_axis_0(std::vector<double> &dct0, int imsize, const PixelData *pixeldata)
{
  std::fill(dct0.begin(), dct0.end(), 0.);

  const int bpp = sizeof(PixelData);
  const double range = (1 << (bpp * 8)) - 1;
  assert(range == 65535 || range == 255);

  const double factor = M_PI / (double) imsize;

  auto coli_row = std::vector<double>(imsize);
  for (int i = 0; i < imsize; ++i) // for every column (axis 1)
  {
    // TODO: replace by lambda? <-- vec(i), vec is a column vector
    // collect values along axis 0
    for (int n = 0; n < imsize; ++n)
    {
      coli_row[n] = std::floor(pixeldata[imsize * n + i] * 255.0 / range);
    }

    for (int k = 0; k < imsize; ++k)
    {
      double &yk = dct0[imsize * k + i];

      for (int n = 0; n < imsize; ++n)
      {
        yk += coli_row[n] * std::cos(k * (n + 0.5) * factor);
      }

      yk *= 2;
    }
  }
}

void dct_axis_0(std::vector<double> &dct0, const QImage &img)
{
  const int imsize = img.width();

  if (img.format() == QImage::Format_Grayscale16)
  {
    return dct_axis_0(dct0, imsize, reinterpret_cast<const uint16_t *>(img.bits()));
  }
  else
  {
    return dct_axis_0(dct0, imsize, img.bits());
  }
}

void dct_axis_1(std::vector<double> &dct1, const std::vector<double> &dct0, int img_size)
{
  const double factor = M_PI / (double) img_size;
  std::fill(dct1.begin(), dct1.end(), 0.);
  for (int i = 0; i < img_size; ++i)
  {
    for (int k = 0; k < img_size; ++k)
    {
      double &y = dct1[img_size * i + k];
      for (int n = 0; n < img_size; ++n) // iterate over axis 1
      {
        y += dct0[img_size * i + n] * std::cos(k * (n + 0.5) * factor);
      }
      y *= 2;
    }
  }
}

inline double sort_and_compute_median(std::vector<double> &values)
{
  std::sort(values.begin(), values.end());
  size_t mid = values.size() / 2;
  return (values.size() % 2 == 1) ? values[mid] : ((values[mid - 1] + values[mid]) / 2.0);
}

void fetch_low_freqs(std::vector<double> &lowfreqs,
                     const std::vector<double> &dct,
                     size_t img_size,
                     size_t hash_size)
{
  assert(lowfreqs.size() == hash_size * hash_size);
  for (int y = 0; y < hash_size; ++y)
  {
    for (int x = 0; x < hash_size; ++x)
    {
      lowfreqs[hash_size * y + x] = dct[img_size * y + x];
    }
  }
}

void hash_from_dct(std::vector<bool> &hash,
                   std::vector<double> &lowfreqs,
                   const std::vector<double> &dct,
                   size_t img_size,
                   size_t hash_size)
{
  fetch_low_freqs(lowfreqs, dct, img_size, hash_size);
  const double m = sort_and_compute_median(lowfreqs);

  fetch_low_freqs(lowfreqs, dct, img_size, hash_size);
  for (size_t i(0); i < hash.size(); ++i)
  {
    hash[i] = lowfreqs[i] > m;
  }
}

quint64 toull(const std::vector<bool> &bits)
{
  quint64 r = 0;
  for (bool b : bits)
  {
    r = (r << 1) | (b ? 1 : 0);
  }
  return r;
}

} // namespace

PerceptualHash::PerceptualHash()
    : m_dct0(32 * 32, 0.)
    , m_dct1(32 * 32, 0.)
    , m_lowfreqs(8 * 8, 0.)
    , m_hashbits(64)
{}

quint64 PerceptualHash::hash(const QImage &image)
{
  Q_ASSERT(!image.isNull());
  if (image.isNull())
  {
    return 0;
  }

  bool size_ok, grayscale_ok;

  if (!checkImage(image))
  {
    QImage img{image};

    if (!size_ok)
    {
      img = img.scaled(QSize(32, 32), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    if (!grayscale_ok)
    {
      if (!img.allGray())
      {
        qDebug() << "image isn't all gray";
      }

      img.convertTo(QImage::Format_Grayscale16);
    }

    return hash(img);
  }

  constexpr size_t hash_size = 8;
  const size_t highfreq_factor = image.width() / hash_size;
  assert(highfreq_factor == 4);

  dct_axis_0(m_dct0, image);
  dct_axis_1(m_dct1, m_dct0, image.width());

  hash_from_dct(m_hashbits, m_lowfreqs, m_dct1, image.width(), hash_size);
  const quint64 hashval = toull(m_hashbits);

  return hashval;
}

quint64 PerceptualHash::hash(const QString &filePath)
{
  QImage image{filePath};

  if (image.isNull())
  {
    qDebug() << "image is null";

    return 0;
  }

  return hash(image);
}

bool PerceptualHash::checkImage(const QImage &image, bool *sizeOk, bool *grayscaleOk)
{
  bool size_ok = image.width() == 32 && image.height() == 32;
  bool grayscale_ok = image.format() != QImage::Format_Grayscale8
                      || image.format() != QImage::Format_Grayscale16;

  if (sizeOk)
  {
    *sizeOk = size_ok;
  }

  if (grayscaleOk)
  {
    *grayscaleOk = grayscale_ok;
  }

  return size_ok && grayscale_ok;
}

quint64 computeHash(const QImage &image)
{
  PerceptualHash hash;
  return hash.hash(image);
}
