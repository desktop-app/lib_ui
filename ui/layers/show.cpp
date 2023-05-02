// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/layers/show.h"

#include "ui/toast/toast.h"

namespace Ui {
namespace {

using namespace Toast;

} // namespace

base::weak_ptr<Instance> Show::showToast(Config &&config) {
	if (const auto strong = _lastToast.get()) {
		strong->hideAnimated();
	}
	_lastToast = valid()
		? Toast::Show(toastParent(), std::move(config))
		: base::weak_ptr<Instance>();
	return _lastToast;
}

base::weak_ptr<Instance> Show::showToast(
		TextWithEntities &&text,
		crl::time duration) {
	return showToast({ .text = std::move(text), .duration = duration });
}

base::weak_ptr<Instance> Show::showToast(
		const QString &text,
		crl::time duration) {
	return showToast({
		.text = TextWithEntities{ text },
		.duration = duration,
	});
}

} // namespace Ui
