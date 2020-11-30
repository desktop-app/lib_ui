// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/unique_qptr.h"
#include "ui/effects/animations.h"
#include "ui/effects/cross_line.h"
#include "ui/widgets/call_button.h"

#include <unordered_map>

namespace Ui {

class BlobsWidget;
class InfiniteRadialAnimation;

enum class CallMuteButtonType {
	Connecting,
	Active,
	Muted,
	ForceMuted,
};

struct CallMuteButtonState {
	QString text;
	CallMuteButtonType type = CallMuteButtonType::Connecting;
};

class CallMuteButton final {
public:
	explicit CallMuteButton(
		not_null<RpWidget*> parent,
		CallMuteButtonState initial = CallMuteButtonState());
	~CallMuteButton();

	void setState(const CallMuteButtonState &state);
	void setLevel(float level);
	[[nodiscard]] rpl::producer<Qt::MouseButton> clicks() const;

	[[nodiscard]] QSize innerSize() const;
	[[nodiscard]] QRect innerGeometry() const;
	void moveInner(QPoint position);

	void setVisible(bool visible);
	void show() {
		setVisible(true);
	}
	void hide() {
		setVisible(false);
	}
	void raise();
	void lower();

	[[nodiscard]] rpl::producer<CallButtonColors> colorOverrides() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void init();
	void contentPaint();
	void overridesColors(
		CallMuteButtonType fromType,
		CallMuteButtonType toType,
		float64 progress);

	void setEnableMouse(bool value);

	rpl::variable<CallMuteButtonState> _state;
	float _level = 0.;
	float64 _crossLineProgress = 0.;
	rpl::variable<float64> _radialShowProgress = 0.;
	QRect _muteIconPosition;

	const base::unique_qptr<BlobsWidget> _blobs;
	CallButton _content;

	std::unique_ptr<InfiniteRadialAnimation> _radial;
	const std::unordered_map<CallMuteButtonType, std::vector<QColor>> _colors;

	CrossLineAnimation _crossLineMuteAnimation;
	Animations::Simple _switchAnimation;

	rpl::event_stream<CallButtonColors> _colorOverrides;

};

} // namespace Ui
