#include "orion/diagnostics/diagnostic_engine.h"
#include "orion/diagnostics/report_contract.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    const QString path = arguments.size() > 1
        ? arguments.at(1)
        : QString::fromUtf8(ORION_DEFAULT_GOLDEN_FIXTURE);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        std::cerr << "Cannot open golden fixture: " << path.toStdString() << '\n';
        return EXIT_FAILURE;
    }

    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        std::cerr << "Invalid golden fixture JSON: " << error.errorString().toStdString() << '\n';
        return EXIT_FAILURE;
    }

    const auto cases = document.array();
    if (cases.size() < 10) {
        std::cerr << "Golden fixture must contain at least ten cases.\n";
        return EXIT_FAILURE;
    }

    QSet<QString> names;
    int expectedFindings = 0;
    int forbiddenFindings = 0;
    int expectedReportFragments = 0;
    int forbiddenReportFragments = 0;
    int passedCases = 0;
    int failures = 0;
    for (const auto& value : cases) {
        if (!value.isObject()) {
            std::cerr << "Golden case is not an object.\n";
            return EXIT_FAILURE;
        }
        const auto item = value.toObject();
        const auto name = item.value(QStringLiteral("name")).toString();
        if (name.isEmpty() || names.contains(name)) {
            std::cerr << "Golden case name is empty or duplicated.\n";
            return EXIT_FAILURE;
        }
        names.insert(name);
        const auto expected = item.value(QStringLiteral("expected")).toArray();
        const auto actualFindings = orion::diagnostics::analyzeSnapshot(item);
        QHash<QString, QJsonObject> actualById;
        for (const auto& actualValue : actualFindings) {
            const auto actual = actualValue.toObject();
            actualById.insert(actual.value(QStringLiteral("id")).toString(), actual);
        }
        bool casePassed = true;
        for (const auto& finding : expected) {
            const auto expectedFinding = finding.toObject();
            const auto id = expectedFinding.value(QStringLiteral("id")).toString();
            if (id.isEmpty()) {
                std::cerr << "Expected finding has no stable id in case "
                          << name.toStdString() << '\n';
                return EXIT_FAILURE;
            }
            if (!actualById.contains(id)) {
                std::cerr << "FAIL [" << name.toStdString()
                          << "]: missing expected finding " << id.toStdString() << '\n';
                casePassed = false;
                continue;
            }
            const auto actual = actualById.value(id);
            for (auto iterator = expectedFinding.constBegin();
                 iterator != expectedFinding.constEnd();
                 ++iterator) {
                if (actual.value(iterator.key()) != iterator.value()) {
                    std::cerr << "FAIL [" << name.toStdString() << "]: "
                              << id.toStdString() << '.' << iterator.key().toStdString()
                              << " differs (expected "
                              << QJsonDocument(QJsonObject {{iterator.key(), iterator.value()}})
                                     .toJson(QJsonDocument::Compact)
                                     .toStdString()
                              << ", actual "
                              << QJsonDocument(QJsonObject {{iterator.key(), actual.value(iterator.key())}})
                                     .toJson(QJsonDocument::Compact)
                                     .toStdString()
                              << ")\n";
                    casePassed = false;
                }
            }
        }
        const auto forbidden = item.value(QStringLiteral("forbidden_ids")).toArray();
        for (const auto& forbiddenValue : forbidden) {
            const auto id = forbiddenValue.toString();
            if (actualById.contains(id)) {
                std::cerr << "FAIL [" << name.toStdString()
                          << "]: forbidden finding was emitted: " << id.toStdString() << '\n';
                casePassed = false;
            }
        }
        const auto reportText = orion::diagnostics::reportToText(
            orion::diagnostics::buildReport(item));
        const auto expectedText = item.value(QStringLiteral("expected_report_contains")).toArray();
        for (const auto& fragmentValue : expectedText) {
            const auto fragment = fragmentValue.toString();
            if (fragment.isEmpty() || !reportText.contains(fragment)) {
                std::cerr << "FAIL [" << name.toStdString()
                          << "]: report is missing expected text: "
                          << fragment.toStdString() << '\n';
                casePassed = false;
            }
        }
        const auto forbiddenText = item.value(QStringLiteral("forbidden_report_contains")).toArray();
        for (const auto& fragmentValue : forbiddenText) {
            const auto fragment = fragmentValue.toString();
            if (!fragment.isEmpty() && reportText.contains(fragment)) {
                std::cerr << "FAIL [" << name.toStdString()
                          << "]: report contains forbidden text: "
                          << fragment.toStdString() << '\n';
                casePassed = false;
            }
        }
        if (casePassed) {
            ++passedCases;
        } else {
            ++failures;
        }
        expectedFindings += expected.size();
        forbiddenFindings += forbidden.size();
        expectedReportFragments += expectedText.size();
        forbiddenReportFragments += forbiddenText.size();
    }

    std::cout << "Golden compatibility: " << passedCases << '/' << cases.size()
              << " cases passed; "
              << expectedFindings << " expected findings and "
              << forbiddenFindings << " forbidden findings; "
              << expectedReportFragments << " expected and "
              << forbiddenReportFragments << " forbidden report fragments.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
#include "orion/diagnostics/diagnostic_engine.h"
