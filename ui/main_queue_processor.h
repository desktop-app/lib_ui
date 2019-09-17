// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace Ui {

class MainQueueProcessor : public QObject {
public:
	MainQueueProcessor();
	~MainQueueProcessor();

protected:
	bool event(QEvent *event) override;

private:
	void acquire();
	void release();

	rpl::lifetime _lifetime;

};

} // namespace Ui
