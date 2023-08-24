/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QWindow;

namespace Ui {
namespace Platform {

class WaylandIntegration {
public:
	WaylandIntegration();
	~WaylandIntegration();

	[[nodiscard]] static WaylandIntegration *Instance();

	[[nodiscard]] bool xdgDecorationSupported();
	void showWindowMenu(not_null<QWidget*> widget, const QPoint &point);

private:
	struct Private;
	const std::unique_ptr<Private> _private;
};

} // namespace Platform
} // namespace Ui
