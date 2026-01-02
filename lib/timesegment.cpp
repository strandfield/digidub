#include "timesegment.h"

#include <QString>
#include <QStringList>

QString Duration::toString(Format format) const
{
  if (format == Format::Seconds)
  {
    int64_t seconds = toMSecs() / 1000;
    int64_t msecs = toMSecs() - seconds * 1000;

    QString text = QString::number(seconds) + ".";

    if (msecs == 0)
    {
      // text += "0";
      text += "000";
      return text;
    }
    else if (msecs < 10)
    {
      text += "00" + QString::number(msecs);
    }
    else if (msecs < 100)
    {
      text += "0" + QString::number(msecs);
    }
    else
    {
      text += QString::number(msecs);
    }

    // while (text.back() == '0')
    // {
    //   text.chop(1);
    // }

    return text;
  }
  else if (format == Format::HHMMSSzzz)
  {
    int64_t val = toMSecs();
    const int64_t h = val / (3600 * 1000);
    val -= h * (3600 * 1000);
    const int64_t m = val / (60 * 1000);
    val -= m * (60 * 1000);

    QString text;

    if (h > 0)
    {
      text += QString::number(h) + ":";
    }

    if (h > 0 && m < 10)
    {
      text += "0";
    }

    text += QString::number(m) + ":";

    if ((val / 1000) < 10)
    {
      text += "0";
    }

    text += Duration(val).toString(Duration::Seconds);

    return text;
  }
  else
  {
    assert(false);
    return QString();
  }
}

Duration Duration::fromString(const QString& text)
{
  Duration d{0};
  if (!d.parse(text))
  {
    d.m_value = -1;
  }
  return d;
}

bool Duration::parse(const QString& text)
{
  if (text.isEmpty())
  {
    m_value = 0;
    return true;
  }

  QStringList parts = text.split(':', Qt::KeepEmptyParts);

  if (parts.size() < 1 || parts.size() > 3)
  {
    return false;
  }

  const double seconds = parts.back().toDouble();
  m_value = std::round(seconds * 1000);

  parts.pop_back();
  if (parts.empty())
  {
    return true;
  }

  auto remove_leading_zeros = [](QString& str) {
    while (str.startsWith("0") && str != "0")
    {
      str = str.mid(1);
    }
  };

  remove_leading_zeros(parts.back());
  const int minutes = parts.back().toInt();
  m_value += minutes * 60 * 1000;

  parts.pop_back();
  if (parts.empty())
  {
    return true;
  }

  remove_leading_zeros(parts.back());
  const int64_t hours = parts.back().toInt();
  m_value += hours * 60 * 60 * 1000;

  return true;
}

QString TimeSegment::toString() const
{
  return Duration(start()).toString(Duration::HHMMSSzzz) + "-"
         + Duration(end()).toString(Duration::HHMMSSzzz);
}

TimeSegment TimeSegment::fromString(const QString& text)
{
  TimeSegment res;

  QStringList parts = text.split('-', Qt::SkipEmptyParts);
  // TODO: handle errors
  assert(parts.size() == 2);

  Duration d{0};
  if (!d.parse(parts.front()))
  {
    // TODO: handle errors
    return res;
  }
  res.m_start = d.toMSecs();

  if (!d.parse(parts.back()))
  {
    // TODO: handle errors
    return res;
  }
  res.m_end = d.toMSecs();

  return res;
}
