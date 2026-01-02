// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include "appsettingkeys.h"

#include <QSettings>

class AppSettings : public QObject
{
  Q_OBJECT
public:
  explicit AppSettings(QObject* parent = nullptr);

  static AppSettings* getInstance(QObject* parent);

  const QSettings& settings() const;

  template<typename T = QVariant>
  T value(const QString& key, const T& defaultValue = T()) const;

  void setValue(const QString& key, const QVariant& value);

  template<typename T, typename Callback>
  void watch(const QString& key, T* object, Callback&& callback) const;

Q_SIGNALS:
  void valueChanged(const QString& key, const QVariant& newValue, const QVariant& oldValue);

protected:
  template<typename T>
  struct type_tag
  {};
  QVariant value(const QString& key, type_tag<QVariant>);

private:
  QSettings m_settings;
};

inline const QSettings& AppSettings::settings() const
{
  return m_settings;
}

template<typename T>
inline T AppSettings::value(const QString& key, const T& defaultValue) const
{
  // return value(key, type_tag<T>());

  if constexpr (std::is_same_v<T, QVariant>)
  {
    return m_settings.value(key, defaultValue);
  }
  else
  {
    QVariant result = m_settings.value(key);

    if (result.isNull())
    {
      return defaultValue;
    }

    return result.value<T>();
  }
}

inline QVariant AppSettings::value(const QString& key, type_tag<QVariant>)
{
  return m_settings.value(key);
}

class SettingsWatcher : public QObject
{
  Q_OBJECT
public:
  explicit SettingsWatcher(AppSettings& settings, const QString& key);

  AppSettings& settings() const;
  const QString& key() const;

Q_SIGNALS:
  void valueChanged(const QVariant& newValue, const QVariant& oldValue);

protected:
  void onSettingValueChanged(const QString& key, const QVariant& newValue, const QVariant& oldValue);

private:
  AppSettings& m_settings;
  QString m_key;
};

template<typename T, typename Callback>
void AppSettings::watch(const QString& key, T* object, Callback&& callback) const
{
  auto* watcher = new SettingsWatcher(const_cast<AppSettings&>(*this), key);
  watcher->setParent(object);
  connect(watcher, &SettingsWatcher::valueChanged, object, std::forward<Callback>(callback));
}

#endif // APPSETTINGS_H
