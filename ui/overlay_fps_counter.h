#pragma once

#include <QElapsedTimer>
#include <QQueue>

namespace orion::app {

class UiFpsCounter final {
public:
    explicit UiFpsCounter(int windowSize = 30);

    void reset();
    [[nodiscard]] double tick();
    [[nodiscard]] double fps() const noexcept;

private:
    int windowSize_ {30};
    QElapsedTimer timer_;
    qint64 lastNanoseconds_ {0};
    bool primed_ {false};
    QQueue<qint64> deltasNanoseconds_;
    double fps_ {0.0};
};

} // namespace orion::app
