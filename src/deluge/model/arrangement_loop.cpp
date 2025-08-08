#include "model/arrangement_loop.h"
#include "util/functions.h"

ArrangementLoop::ArrangementLoop() {
	clear();
}

void ArrangementLoop::create(int32_t startPos, int32_t endPos) {
	if (!isValidRange(startPos, endPos)) {
		return;
	}

	startPos_ = startPos;
	endPos_ = endPos;
	exists_ = true;
	active_ = true;          // Auto-activate when created
	playheadInside_ = false; // Reset state - will be set when playhead enters loop region
}

void ArrangementLoop::clear() {
	exists_ = false;
	active_ = false;
	playheadInside_ = false;
	startPos_ = -1;
	endPos_ = -1;
}

void ArrangementLoop::updatePlayheadState(int32_t currentPos) {
	if (!exists_) {
		playheadInside_ = false;
		return;
	}

	// Check if we're currently within loop boundaries
	bool currentlyInside = (currentPos >= startPos_ && currentPos < endPos_);

	// If we just entered the loop, set the flag
	if (currentlyInside && !playheadInside_) {
		playheadInside_ = true;
	}
	// If we've moved outside the loop (before start), clear the flag
	else if (currentPos < startPos_) {
		playheadInside_ = false;
	}
	// Note: We don't clear the flag when reaching the end - that's handled by the loop logic
}

void ArrangementLoop::initializePlayheadState(int32_t currentPos) {
	if (!exists_) {
		playheadInside_ = false;
		return;
	}

	// Set initial state based on current position
	playheadInside_ = (currentPos >= startPos_ && currentPos < endPos_);
}

bool ArrangementLoop::containsPosition(int32_t pos) const {
	if (!exists_) {
		return false;
	}
	return pos >= startPos_ && pos < endPos_;
}

bool ArrangementLoop::shouldLoopAtPosition(int32_t pos) const {
	if (!isActive()) {
		return false;
	}
	// Only loop back if we've been inside the loop and now reached the end
	// This prevents jarring jumps when creating loops behind the playhead
	return playheadInside_ && pos >= endPos_;
}

bool ArrangementLoop::isValidRange(int32_t start, int32_t end) const {
	return start >= 0 && end > start && end < 2147483647;
}

void ArrangementLoop::setFromDeserialized(int32_t startPos, int32_t endPos, bool active) {
	if (!isValidRange(startPos, endPos)) {
		clear();
		return;
	}

	startPos_ = startPos;
	endPos_ = endPos;
	exists_ = true;
	active_ = active;
	playheadInside_ = false; // Reset playhead state on load
}
