#ifndef LOGCPP_QT_FORMATTER_H
#define LOGCPP_QT_FORMATTER_H

#include <QByteArray>
#include <QString>
#include <spdlog/fmt/fmt.h>

template <>
struct fmt::formatter<QString> {
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
        auto it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            throw format_error("invalid format for QString");
        }
        return it;
    }

    template <typename FormatContext>
    auto format(const QString &value, FormatContext &ctx) const -> decltype(ctx.out()) {
        const QByteArray utf8 = value.toUtf8();
        return format_to(ctx.out(), "{}", utf8.constData());
    }
};

template <>
struct fmt::formatter<QByteArray> {
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
        auto it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            throw format_error("invalid format for QByteArray");
        }
        return it;
    }

    template <typename FormatContext>
    auto format(const QByteArray &value, FormatContext &ctx) const -> decltype(ctx.out()) {
        return format_to(ctx.out(), "{}", value.constData());
    }
};

#endif
