// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/platform/ui_platform_window.h"

namespace Ui {
namespace Platform {

class TitleWidget;

class WindowHelper final : public BasicWindowHelper {
public:
	explicit WindowHelper(not_null<RpWidget*> window);
	~WindowHelper();

	not_null<RpWidget*> body() override;
	void setTitle(const QString &title) override;
	void setTitleStyle(const style::WindowTitle &st) override;
	void setMinimumSize(QSize size) override;
	void setFixedSize(QSize size) override;
	void setGeometry(QRect rect) override;
	void close() override;

private:
	class Private;
	friend class Private;

	void setupBodyTitleAreaEvents() override;

	void init();
	void toggleCustomTitle(bool visible);

	const std::unique_ptr<Private> _private;
	const not_null<TitleWidget*> _title;
	const not_null<RpWidget*> _body;

#ifdef OS_OSX
	struct WindowDrag {
		QPoint windowStartPosition;
		QPoint dragStartPosition;
	};
	std::optional<WindowDrag> _drag;
#endif // OS_OSX

};

} // namespace Platform
} // namespace Ui
