#include "appsettings.h"

AppSettings::AppSettings(QObject* parent)
    : QObject(parent)
{}

AppSettings* AppSettings::getInstance(QObject* parent)
{
  return parent->findChild<AppSettings*>();
}

void AppSettings::setValue(const QString& key, const QVariant& value)
{
  const QVariant oldval = this->value(key);
  if (oldval != value)
  {
    m_settings.setValue(key, value);
    Q_EMIT valueChanged(key, value, oldval);
  }
}

SettingsWatcher::SettingsWatcher(AppSettings& settings, const QString& key)
    : QObject(nullptr)
    , m_settings(settings)
    , m_key(key)
{
  connect(&settings, &AppSettings::valueChanged, this, &SettingsWatcher::onSettingValueChanged);
}

AppSettings& SettingsWatcher::settings() const
{
  return m_settings;
}

const QString& SettingsWatcher::key() const
{
  return m_key;
}

void SettingsWatcher::onSettingValueChanged(const QString& key,
                                            const QVariant& newValue,
                                            const QVariant& oldValue)
{
  if (m_key == key)
  {
    Q_EMIT valueChanged(newValue, oldValue);
  }
}
