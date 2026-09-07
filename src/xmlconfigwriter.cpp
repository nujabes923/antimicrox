/* antimicrox Gamepad to KB+M event mapper
 * Copyright (C) 2015 Travis Nickles <nickles.travis@gmail.com>
 * Copyright (C) 2020 Jagoda Górska <juliagoda.pl@protonmail>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "xmlconfigwriter.h"

#include "common.h"
#include "inputdevice.h"
#include "xml/inputdevicexml.h"

#include <QDateTime>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QXmlStreamWriter>

XMLConfigWriter::XMLConfigWriter(QObject *parent)
    : QObject(parent)
{
    xml = new QXmlStreamWriter();
    xml->setAutoFormatting(true);
    configFile = nullptr;
    m_joystick = nullptr;
    m_joystickXml = nullptr;
    writerError = false;
}

XMLConfigWriter::~XMLConfigWriter()
{
    if (configFile != nullptr)
    {
        if (configFile->isOpen())
            configFile->close();

        delete configFile;
        configFile = nullptr;
    }

    if (xml != nullptr)
    {
        delete xml;
        xml = nullptr;
    }
}

/**
 * @brief Write input device config from the current object into XML file
 * @param[in] joystickXml InputDeviceXml which gets serialized
 */
void XMLConfigWriter::write(InputDeviceXml *joystickXml)
{
    writerError = false;
    writerErrorString.clear();
    const QString target = fileName;
    if (target.isEmpty())
    {
        writerError = true;
        writerErrorString = tr("No profile filename selected.");
        return;
    }

    // Keep the existing file intact until both its backup and the new XML are safe.
    if (QFileInfo::exists(target))
    {
        const QFileInfo info(target);
        QDir directory(info.absolutePath());
        const QString backupDirectory = directory.filePath(".antimicrox-backups");
        const QString backupName = info.completeBaseName() + "." +
            QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + "." +
            QUuid::createUuid().toString(QUuid::Id128) + "." + info.suffix();
        if (!directory.mkpath(backupDirectory) || !QFile::copy(target, QDir(backupDirectory).filePath(backupName)))
        {
            writerError = true;
            writerErrorString = tr("Could not back up %1. The original profile was not changed.").arg(target);
            return;
        }
    }

    QSaveFile output(target);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        writerError = true;
        writerErrorString = tr("Could not write to profile at %1: %2").arg(target, output.errorString());
        return;
    }
    xml->setDevice(&output);
    xml->writeStartDocument();
    joystickXml->writeConfig(xml);
    xml->writeEndDocument();
    if (xml->hasError())
    {
        output.cancelWriting();
        writerError = true;
    } else if (!output.commit())
        writerError = true;
    xml->setDevice(nullptr);
    if (writerError)
        writerErrorString = tr("Could not save %1. The original profile was preserved.").arg(target);
}

/**
 * @brief Sets the filename of the to be written XML file
 */
void XMLConfigWriter::setFileName(QString filename)
{
    delete configFile;
    QFile *temp = new QFile(filename);
    fileName = filename;
    configFile = temp;
}

bool XMLConfigWriter::hasError() { return writerError; }

const QString XMLConfigWriter::getErrorString() { return writerErrorString; }

const QXmlStreamWriter *XMLConfigWriter::getXml() { return xml; }

QString const &XMLConfigWriter::getFileName() { return fileName; }

const QFile *XMLConfigWriter::getConfigFile() { return configFile; }

const InputDevice *XMLConfigWriter::getJoystick() { return m_joystick; }
