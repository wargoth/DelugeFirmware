#pragma once

#include "definitions_cxx.hpp"

/**
 * @brief Represents a loop region in arrangement mode
 *
 * This class encapsulates all loop-related state and behavior,
 * providing a clean interface for loop management.
 */
class ArrangementLoop {
public:
	ArrangementLoop();

	// Core loop management
	void create(int32_t startPos, int32_t endPos);
	void clear();
	void setActive(bool active) { active_ = active; }

	// Playhead state management (prevents jarring jumps when creating loops behind playhead)
	void updatePlayheadState(int32_t currentPos);
	void resetPlayheadState() { playheadInside_ = false; }
	void initializePlayheadState(int32_t currentPos); // Set initial state when activating loop during playback
	bool isPlayheadInside() const { return playheadInside_; }

	// State queries
	bool exists() const { return exists_; }
	bool isActive() const { return exists_ && active_; }
	bool containsPosition(int32_t pos) const;
	bool shouldLoopAtPosition(int32_t pos) const;

	// Position accessors
	int32_t getStart() const { return startPos_; }
	int32_t getEnd() const { return endPos_; }
	int32_t getLength() const { return exists_ ? (endPos_ - startPos_) : 0; }

	// Playback helpers
	int32_t getLoopStartPosition() const { return startPos_; }
	bool isPositionAtEnd(int32_t pos) const { return pos >= endPos_; }

	// Serialization
	void writeToFile() const;
	void readFromFile();

private:
	bool exists_{false};
	bool active_{false};
	bool playheadInside_{false}; // Track if playhead is currently inside loop region
	int32_t startPos_{-1};
	int32_t endPos_{-1};

	// Validation
	bool isValidRange(int32_t start, int32_t end) const;
};
