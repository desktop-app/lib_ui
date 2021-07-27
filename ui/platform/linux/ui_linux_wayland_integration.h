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
	[[nodiscard]] static WaylandIntegration *Instance();

	void waitForInterfaceAnnounce();
	[[nodiscard]] bool xdgDecorationSupported();
	[[nodiscard]] bool windowExtentsSupported();
	void setWindowExtents(QWindow *window, const QMargins &extents);
	void unsetWindowExtents(QWindow *window);
	bool showWindowMenu(QWindow *window);

private:
	WaylandIntegration();
	~WaylandIntegration();

	struct Private;
	const std::unique_ptr<Private> _private;
};

} // namespace Platform
} // namespace Ui
