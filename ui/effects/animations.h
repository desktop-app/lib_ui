// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/effects/animation_value.h"

#include <crl/crl_time.h>
#include <rpl/lifetime.h>
#include <QtCore/QObject>

namespace Ui {
namespace Animations {

class Manager;

class Basic final {
public:
	Basic() = default;
	Basic(const Basic &other) = delete;
	Basic &operator=(const Basic &other) = delete;

	template <typename Callback>
	explicit Basic(Callback &&callback);

	template <typename Callback>
	void init(Callback &&callback);

	void start();
	void stop();

	[[nodiscard]] crl::time started() const;
	[[nodiscard]] bool animating() const;

	~Basic();

private:
	friend class Manager;

	template <typename Callback>
	[[nodiscard]] static Fn<bool(crl::time)> Prepare(Callback &&callback);

	[[nodiscard]] bool call(crl::time now) const;
	void restart();

	void markStarted();
	void markStopped();

	crl::time _started = -1;
	Fn<bool(crl::time)> _callback;

};

class Simple final {
public:
	template <typename Callback>
	void start(
		Callback &&callback,
		float64 from,
		float64 to,
		crl::time duration,
		anim::transition transition = anim::linear);
	void change(
		float64 to,
		crl::time duration,
		anim::transition transition = anim::linear);
	void stop();
	[[nodiscard]] bool animating() const;
	[[nodiscard]] float64 value(float64 final) const;

private:
	class ShortTracker {
	public:
		ShortTracker() {
			restart();
		}
		ShortTracker(const ShortTracker &other) = delete;
		ShortTracker &operator=(const ShortTracker &other) = delete;
		~ShortTracker() {
			release();
		}
		void restart() {
			if (!std::exchange(_paused, true)) {
				style::internal::StartShortAnimation();
			}
		}
		void release() {
			if (std::exchange(_paused, false)) {
				style::internal::StopShortAnimation();
			}
		}

	private:
		bool _paused = false;

	};

	struct Data {
		explicit Data(float64 initial) : value(initial) {
		}
		~Data() {
			if (markOnDelete) {
				*markOnDelete = true;
			}
		}

		Basic animation;
		anim::transition transition;
		float64 from = 0.;
		float64 delta = 0.;
		float64 value = 0.;
		float64 duration = 0.;
		bool *markOnDelete = nullptr;
		ShortTracker tracker;
	};

	template <typename Callback>
	[[nodiscard]] static decltype(auto) Prepare(Callback &&callback);

	void prepare(float64 from, crl::time duration);
	void startPrepared(
		float64 to,
		crl::time duration,
		anim::transition transition);

	static constexpr auto kLongAnimationDuration = crl::time(1000);

	mutable std::unique_ptr<Data> _data;

};

class Manager final : private QObject {
public:
	Manager();
	~Manager();

	void update();

private:
	class ActiveBasicPointer {
	public:
		ActiveBasicPointer(Basic *value = nullptr) : _value(value) {
			if (_value) {
				_value->markStarted();
			}
		}
		ActiveBasicPointer(ActiveBasicPointer &&other)
		: _value(base::take(other._value)) {
		}
		ActiveBasicPointer &operator=(ActiveBasicPointer &&other) {
			if (_value != other._value) {
				if (_value) {
					_value->markStopped();
				}
				_value = base::take(other._value);
			}
			return *this;
		}
		~ActiveBasicPointer() {
			if (_value) {
				_value->markStopped();
			}
		}

		[[nodiscard]] bool call(crl::time now) const {
			return _value && _value->call(now);
		}

		friend inline bool operator==(
				const ActiveBasicPointer &a,
				const ActiveBasicPointer &b) {
			return a._value == b._value;
		}

		Basic *get() const {
			return _value;
		}

	private:
		Basic *_value = nullptr;

	};

	friend class Basic;

	void timerEvent(QTimerEvent *e) override;

	void start(not_null<Basic*> animation);
	void stop(not_null<Basic*> animation);

	void schedule();
	void updateQueued();
	void stopTimer();
	not_null<const QObject*> delayedCallGuard() const;

	crl::time _lastUpdateTime = 0;
	int _timerId = 0;
	bool _updating = false;
	bool _removedWhileUpdating = false;
	bool _scheduled = false;
	bool _forceImmediateUpdate = false;
	std::vector<ActiveBasicPointer> _active;
	std::vector<ActiveBasicPointer> _starting;
	rpl::lifetime _lifetime;

};

template <typename Callback>
Fn<bool(crl::time)> Basic__PrepareCrlTime(Callback &&callback) {
	using Return = decltype(callback(crl::time(0)));
	if constexpr (std::is_convertible_v<Return, bool>) {
		return std::forward<Callback>(callback);
	} else if constexpr (std::is_same_v<Return, void>) {
		return [callback = std::forward<Callback>(callback)](
			crl::time time) {
			callback(time);
			return true;
		};
	} else {
		static_assert(false_t(callback), "Expected void or bool.");
	}
}

template <typename Callback>
Fn<bool(crl::time)> Basic__PreparePlain(Callback &&callback) {
	using Return = decltype(callback());
	if constexpr (std::is_convertible_v<Return, bool>) {
		return [callback = std::forward<Callback>(callback)](crl::time) {
			return callback();
		};
	} else if constexpr (std::is_same_v<Return, void>) {
		return [callback = std::forward<Callback>(callback)](crl::time) {
			callback();
			return true;
		};
	} else {
		static_assert(false_t(callback), "Expected void or bool.");
	}
}

template <typename Callback>
inline Fn<bool(crl::time)> Basic::Prepare(Callback &&callback) {
	if constexpr (rpl::details::is_callable_plain_v<Callback, crl::time>) {
		return Basic__PrepareCrlTime(std::forward<Callback>(callback));
	} else if constexpr (rpl::details::is_callable_plain_v<Callback>) {
		return Basic__PreparePlain(std::forward<Callback>(callback));
	} else {
		static_assert(false_t(callback), "Expected crl::time or no args.");
	}
}

template <typename Callback>
inline Basic::Basic(Callback &&callback)
: _callback(Prepare(std::forward<Callback>(callback))) {
}

template <typename Callback>
inline void Basic::init(Callback &&callback) {
	_callback = Prepare(std::forward<Callback>(callback));
}

TG_FORCE_INLINE crl::time Basic::started() const {
	return _started;
}

TG_FORCE_INLINE bool Basic::animating() const {
	return (_started >= 0);
}

TG_FORCE_INLINE bool Basic::call(crl::time now) const {
	Expects(_started >= 0);

	// _started may be greater than now if we called restart while iterating.
	const auto onstack = _callback;
	return onstack(std::max(_started, now));
}

inline Basic::~Basic() {
	stop();
}

template <typename Callback>
decltype(auto) Simple__PrepareFloat64(Callback &&callback) {
	using Return = decltype(callback(float64(0.)));
	if constexpr (std::is_convertible_v<Return, bool>) {
		return std::forward<Callback>(callback);
	} else if constexpr (std::is_same_v<Return, void>) {
		return [callback = std::forward<Callback>(callback)](
			float64 value) {
			callback(value);
			return true;
		};
	} else {
		static_assert(false_t(callback), "Expected void or float64.");
	}
}

template <typename Callback>
decltype(auto) Simple__PreparePlain(Callback &&callback) {
	using Return = decltype(callback());
	if constexpr (std::is_convertible_v<Return, bool>) {
		return [callback = std::forward<Callback>(callback)](float64) {
			return callback();
		};
	} else if constexpr (std::is_same_v<Return, void>) {
		return [callback = std::forward<Callback>(callback)](float64) {
			callback();
			return true;
		};
	} else {
		static_assert(false_t(callback), "Expected void or bool.");
	}
}

template <typename Callback>
decltype(auto) Simple::Prepare(Callback &&callback) {
	if constexpr (rpl::details::is_callable_plain_v<Callback, float64>) {
		return Simple__PrepareFloat64(std::forward<Callback>(callback));
	} else if constexpr (rpl::details::is_callable_plain_v<Callback>) {
		return Simple__PreparePlain(std::forward<Callback>(callback));
	} else {
		static_assert(false_t(callback), "Expected float64 or no args.");
	}
}

template <typename Callback>
inline void Simple::start(
		Callback &&callback,
		float64 from,
		float64 to,
		crl::time duration,
		anim::transition transition) {
	prepare(from, duration);
	_data->animation.init([
		that = _data.get(),
		callback = Prepare(std::forward<Callback>(callback))
	](crl::time now) {
		Assert(!std::isnan(double(now - that->animation.started())));
		const auto time = anim::Disabled()
			? that->duration
			: (now - that->animation.started());
		Assert(!std::isnan(time));
		Assert(!std::isnan(that->delta));
		Assert(!std::isnan(that->duration));
		const auto finished = (time >= that->duration);
		Assert(finished || that->duration > 0);
		const auto progressRatio = finished ? 1. : time / that->duration;
		Assert(!std::isnan(progressRatio));
		const auto progress = finished
			? that->delta
			: that->transition(that->delta, progressRatio);
		Assert(!std::isnan(that->from));
		Assert(!std::isnan(progress));
		that->value = that->from + progress;
		Assert(!std::isnan(that->value));

		if (finished) {
			that->animation.stop();
		}

		auto deleted = false;
		that->markOnDelete = &deleted;
		const auto result = callback(that->value) && !finished;
		if (!deleted) {
			that->markOnDelete = nullptr;
			if (!result) {
				that->tracker.release();
			}
		}
		return result;
	});
	startPrepared(to, duration, transition);
}

inline void Simple::change(
		float64 to,
		crl::time duration,
		anim::transition transition) {
	Expects(_data != nullptr);

	prepare(0. /* ignored */, duration);
	startPrepared(to, duration, transition);
}

inline void Simple::prepare(float64 from, crl::time duration) {
	const auto isLong = (duration > kLongAnimationDuration);
	if (!_data) {
		_data = std::make_unique<Data>(from);
	} else if (!isLong) {
		_data->tracker.restart();
	}
	if (isLong) {
		_data->tracker.release();
	}
}

inline void Simple::stop() {
	_data = nullptr;
}

inline bool Simple::animating() const {
	if (!_data) {
		return false;
	} else if (!_data->animation.animating()) {
		_data = nullptr;
		return false;
	}
	return true;
}

TG_FORCE_INLINE float64 Simple::value(float64 final) const {
	if (animating()) {
		Assert(!std::isnan(_data->value));
		return _data->value;
	}
	Assert(!std::isnan(final));
	return final;
}

inline void Simple::startPrepared(
		float64 to,
		crl::time duration,
		anim::transition transition) {
	_data->from = _data->value;
	_data->delta = to - _data->from;
	_data->duration = duration * anim::SlowMultiplier();
	_data->transition = transition;
	_data->animation.start();
}

} // namespace Animations
} // namespace Ui
