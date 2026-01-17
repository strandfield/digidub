#include "wav.h"

#include <QFile>

#include <QDebug>

struct WavHeader
{
  char chunk_ID[4];    //  4  riff_mark[4];
  uint32_t chunk_size; //  4  file_size;
  char format[4];      //  4  wave_str[4];
};

struct ChunkHeader
{
  char chunk_ID[4];
  uint32_t chunk_size;
};

struct FmtChunk
{
  ChunkHeader header;
  uint16_t audio_format;    //  2  pcm_encode;
  uint16_t num_channels;    //  2  sound_channel;
  uint32_t sample_rate;     //  4  pcm_sample_freq;
  uint32_t byte_rate;       //  4  byte_freq;
  uint16_t block_align;     //  2  block_align;
  uint16_t bits_per_sample; //  2  sample_bits;
};

struct DataChunk
{
  ChunkHeader header;
  std::vector<int16_t> data; // TODO: add support for 8-bit per sample WAV files
};

struct WavFile
{
  WavHeader header;
  FmtChunk fmt;
  DataChunk data;
};

std::vector<WavSample> readWav(const QString& filePath)
{
  QFile file{filePath};
  if (!file.open(QIODevice::ReadOnly))
  {
    qDebug() << "could not open" << filePath;
    return {};
  }

  // Read the WAV header
  WavFile wav;
  file.read(reinterpret_cast<char*>(&wav), sizeof(WavHeader));

  // If the file is a valid WAV file
  if (std::string(wav.header.chunk_ID, 4) != "WAVE" && std::string(wav.header.chunk_ID, 4) != "RIFF")
  {
    qDebug() << "Not a WAVE or RIFF!";
    return {};
  }

  while (!file.atEnd())
  {
    ChunkHeader chkheader;
    file.read(reinterpret_cast<char*>(&chkheader), sizeof(ChunkHeader));

    if (std::string(chkheader.chunk_ID, 4) == "fmt ")
    {
      wav.fmt.header = chkheader;
      file.read(reinterpret_cast<char*>(&wav.fmt) + sizeof(ChunkHeader),
                sizeof(FmtChunk) - sizeof(ChunkHeader));
    }
    else if (std::string(chkheader.chunk_ID, 4) == "data")
    {
      wav.data.header = chkheader;
      auto audio_data = std::vector<int16_t>(wav.data.header.chunk_size / sizeof(int16_t));
      file.read(reinterpret_cast<char*>(audio_data.data()), wav.data.header.chunk_size);
      file.close();
      wav.data.data = std::move(audio_data);

      break;
    }
    else
    {
      qDebug() << "skipping unknown chunk " << std::string(chkheader.chunk_ID, 4);
      file.seek(file.pos() + chkheader.chunk_size);
    }
  }

  const int64_t duration = (1000 * 8 * int64_t(wav.data.header.chunk_size))
                           / int64_t(wav.fmt.num_channels * wav.fmt.bits_per_sample)
                           / int64_t(wav.fmt.sample_rate);
  const double length_in_seconds = duration / 1000.0;

  qDebug() << "FileName:" << filePath;
  qDebug() << "File size:" << wav.header.chunk_size + 8;
  qDebug() << "Resource Exchange File Mark:" << std::string(wav.header.chunk_ID, 4);
  qDebug() << "Format:" << std::string(wav.header.format, 4);

  qDebug() << "Channels: " << wav.fmt.num_channels;
  qDebug() << "Sample Rate: " << wav.fmt.sample_rate << " Hz";
  qDebug() << "Bits Per Sample: " << wav.fmt.bits_per_sample << " bits";
  qDebug() << "Estimated length: " << length_in_seconds << " seconds";

  if (wav.fmt.num_channels != 1)
  {
    qDebug() << "Only 1-channel wav are supported";
    return {};
  }

  if (wav.fmt.bits_per_sample != 16)
  {
    qDebug() << "Only 16-bit samples wav are supported";
    return {};
  }

  std::vector<WavSample> result;
  result.reserve((duration / 10) + 1);
  size_t i = 0;

  while (i * 10 < duration)
  {
    const size_t start_index = (i * wav.fmt.sample_rate / 100);
    const size_t end_index = ((i + 1) * wav.fmt.sample_rate / 100);

    const auto begin = wav.data.data.begin() + std::min(start_index, wav.data.data.size());
    const auto end = wav.data.data.begin() + std::min(end_index, wav.data.data.size());

    if (begin == end)
    {
      break;
    }

    int highcum = 0, lowcum = 0;
    int nhigh = 0, nlow = 0;
    int maxval = 0;
    int minval = 0;

    for (auto it = begin; it != end; ++it)
    {
      int16_t sample = *it;

      maxval = std::max<int>(maxval, sample);
      minval = std::min<int>(minval, sample);

      if (sample < 0)
      {
        lowcum += sample;
        ++nlow;
      }
      else
      {
        highcum += sample;
        ++nhigh;
      }
    }

    const int highavg = nhigh ? 255 * highcum / (std::numeric_limits<int16_t>::max() * nhigh) : 0;
    const int lowavg = nlow ? 255 * lowcum / (std::numeric_limits<int16_t>::min() * nlow) : 0;

    maxval = 255 * maxval / std::numeric_limits<int16_t>::max();
    minval = 255 * minval / std::numeric_limits<int16_t>::min();

    result.push_back(makeWavSample(maxval, minval));
    ++i;
  }

  return result;
}
