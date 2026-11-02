#ifndef METADATATABLE_HPP
#define METADATATABLE_HPP

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

/*!
 * \brief Parsed content of one metadata CSV file.
 *
 * A table is filled once by the loader thread and never modified after it
 * has been handed over, so any thread may read it without locking.
 */
struct MetadataTable
{
    QString path;
    QString errorString;

    QByteArray headerLine;          // raw header, kept for exporting
    QStringList columns;            // trimmed column names
    int idColumn = -1;
    int panColumn = -1;
    int tiltColumn = -1;
    int modeColumn = -1;

    QVector<qint64> ids;            // one entry per data row
    QVector<QByteArray> lines;      // raw data rows, kept byte for byte
    QHash<qint64, int> rowById;     // frame id -> row index

    int skippedLines = 0;           // malformed rows
    int duplicateIds = 0;
    int missingIds = 0;             // gaps between the first and last id
    qint64 firstId = -1;
    qint64 lastId = -1;
};

#endif // METADATATABLE_HPP
