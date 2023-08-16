// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/main_queue_processor.h"

#include "base/integration.h"
#include "ui/platform/ui_platform_utility.h"

#include <QtCore/QMutex>
#include <QtCore/QCoreApplication>
#include <QtGui/QtEvents>

#include <crl/crl_on_main.h>

namespace Ui {
namespace {

auto ProcessorEventType() {
	static const auto Result = QEvent::Type(QEvent::registerEventType());
	return Result;
}

QMutex ProcessorMutex;
MainQueueProcessor *ProcessorInstance/* = nullptr*/;

enum class ProcessState : int {
	Processed,
	FillingUp,
	Waiting,
};

std::atomic<ProcessState> MainQueueProcessState/* = ProcessState(0)*/;
void (*MainQueueProcessCallback)(void*)/* = nullptr*/;
void *MainQueueProcessArgument/* = nullptr*/;

void PushToMainQueueGeneric(void (*callable)(void*), void *argument) {
	Expects(Platform::UseMainQueueGeneric());

	auto expected = ProcessState::Processed;
	const auto fill = MainQueueProcessState.compare_exchange_strong(
		expected,
		ProcessState::FillingUp);
	if (fill) {
		MainQueueProcessCallback = callable;
		MainQueueProcessArgument = argument;
		MainQueueProcessState.store(ProcessState::Waiting);
	}

	auto event = std::make_unique<QEvent>(ProcessorEventType());

	QMutexLocker lock(&ProcessorMutex);
	if (ProcessorInstance) {
		QCoreApplication::postEvent(ProcessorInstance, event.release());
	}
}

void DrainMainQueueGeneric() {
	Expects(Platform::UseMainQueueGeneric());

	if (MainQueueProcessState.load() != ProcessState::Waiting) {
		return;
	}
	const auto callback = MainQueueProcessCallback;
	const auto argument = MainQueueProcessArgument;
	MainQueueProcessState.store(ProcessState::Processed);

	callback(argument);
}

} // namespace

MainQueueProcessor::MainQueueProcessor() {
	if constexpr (Platform::UseMainQueueGeneric()) {
		acquire();
		crl::init_main_queue(PushToMainQueueGeneric);
	} else {
		crl::wrap_main_queue([](void (*callable)(void*), void *argument) {
			base::Integration::Instance().enterFromEventLoop([&] {
				callable(argument);
			});
		});
	}

	crl::on_main_update_requests(
	) | rpl::start_with_next([] {
		if constexpr (Platform::UseMainQueueGeneric()) {
			DrainMainQueueGeneric();
		} else {
			Platform::DrainMainQueue();
		}
	}, _lifetime);
}

bool MainQueueProcessor::event(QEvent *event) {
	if constexpr (Platform::UseMainQueueGeneric()) {
		if (event->type() == ProcessorEventType()) {
			DrainMainQueueGeneric();
			return true;
		}
	}
	return QObject::event(event);
}

void MainQueueProcessor::acquire() {
	Expects(Platform::UseMainQueueGeneric());
	Expects(ProcessorInstance == nullptr);

	QMutexLocker lock(&ProcessorMutex);
	ProcessorInstance = this;
}

void MainQueueProcessor::release() {
	Expects(Platform::UseMainQueueGeneric());
	Expects(ProcessorInstance == this);

	QMutexLocker lock(&ProcessorMutex);
	ProcessorInstance = nullptr;
}

MainQueueProcessor::~MainQueueProcessor() {
	if constexpr (Platform::UseMainQueueGeneric()) {
		release();
	}
}

} // namespace Ui
