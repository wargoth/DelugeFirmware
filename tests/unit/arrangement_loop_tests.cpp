#include "CppUTest/TestHarness.h"
#include "model/arrangement_loop.h"

TEST_GROUP(ArrangementLoopTests) {
	ArrangementLoop loop;

	void setup() {
		loop.clear();
	}

	void teardown() {
		loop.clear();
	}
};

TEST(ArrangementLoopTests, InitialState) {
	CHECK_FALSE(loop.exists());
	CHECK_FALSE(loop.isActive());
	CHECK_FALSE(loop.isPlayheadInside());
	CHECK_EQUAL(-1, loop.getStart());
	CHECK_EQUAL(-1, loop.getEnd());
	CHECK_EQUAL(0, loop.getLength());
}

TEST(ArrangementLoopTests, CreateValidLoop) {
	loop.create(100, 500);
	CHECK_TRUE(loop.exists());
	CHECK_TRUE(loop.isActive()); // Auto-activated when created
	CHECK_EQUAL(100, loop.getStart());
	CHECK_EQUAL(500, loop.getEnd());
	CHECK_EQUAL(400, loop.getLength());
}

TEST(ArrangementLoopTests, CreateInvalidLoop) {
	// End position before start position
	loop.create(500, 100);
	CHECK_FALSE(loop.exists()); // Should not create invalid loop

	// Zero-length loop
	loop.create(100, 100);
	CHECK_FALSE(loop.exists()); // Should not create zero-length loop
}

TEST(ArrangementLoopTests, SetActive) {
	loop.create(100, 500);

	CHECK_TRUE(loop.isActive()); // Auto-activated when created
	loop.setActive(false);
	CHECK_FALSE(loop.isActive());
	loop.setActive(true);
	CHECK_TRUE(loop.isActive());
}

TEST(ArrangementLoopTests, Clear) {
	loop.create(100, 500);
	loop.setActive(true);

	CHECK_TRUE(loop.exists());
	CHECK_TRUE(loop.isActive());

	loop.clear();

	CHECK_FALSE(loop.exists());
	CHECK_FALSE(loop.isActive());
	CHECK_FALSE(loop.isPlayheadInside());
	CHECK_EQUAL(-1, loop.getStart());
	CHECK_EQUAL(-1, loop.getEnd());
}

TEST(ArrangementLoopTests, ContainsPosition) {
	loop.create(100, 500);

	CHECK_FALSE(loop.containsPosition(50));  // Before loop
	CHECK_TRUE(loop.containsPosition(100));  // At start
	CHECK_TRUE(loop.containsPosition(300));  // Inside loop
	CHECK_TRUE(loop.containsPosition(499));  // Just before end
	CHECK_FALSE(loop.containsPosition(500)); // At end (exclusive)
	CHECK_FALSE(loop.containsPosition(600)); // After loop
}

TEST(ArrangementLoopTests, ShouldLoopAtPosition) {
	loop.create(100, 500);

	// Loop doesn't exist, should never loop
	loop.clear();
	CHECK_FALSE(loop.shouldLoopAtPosition(500));

	// Loop exists and auto-activated, but playhead hasn't been inside yet
	loop.create(100, 500);
	CHECK_FALSE(loop.shouldLoopAtPosition(500)); // playheadInside_ is false initially

	// Only loops after playhead has been inside and reaches end
	loop.initializePlayheadState(300);           // Set playhead as inside
	CHECK_FALSE(loop.shouldLoopAtPosition(499)); // Before end
	CHECK_TRUE(loop.shouldLoopAtPosition(500));  // At end, and playhead was inside
	CHECK_TRUE(loop.shouldLoopAtPosition(501));  // After end, and playhead was inside

	// Loop exists but deactivated
	loop.setActive(false);
	CHECK_FALSE(loop.shouldLoopAtPosition(500)); // Inactive loops don't trigger
}

TEST(ArrangementLoopTests, PlayheadStateTracking) {
	loop.create(100, 500);

	// Initialize playhead state
	loop.initializePlayheadState(50); // Before loop
	CHECK_FALSE(loop.isPlayheadInside());

	loop.initializePlayheadState(300); // Inside loop
	CHECK_TRUE(loop.isPlayheadInside());

	loop.initializePlayheadState(600); // After loop
	CHECK_FALSE(loop.isPlayheadInside());
}

TEST(ArrangementLoopTests, UpdatePlayheadState) {
	loop.create(100, 500);
	loop.initializePlayheadState(50); // Start outside
	CHECK_FALSE(loop.isPlayheadInside());

	// Move into loop
	loop.updatePlayheadState(300);
	CHECK_TRUE(loop.isPlayheadInside());

	// Move within loop
	loop.updatePlayheadState(400);
	CHECK_TRUE(loop.isPlayheadInside());

	// Move to end of loop - updatePlayheadState doesn't clear the flag at end
	loop.updatePlayheadState(500);
	CHECK_TRUE(loop.isPlayheadInside()); // Still true - not cleared by updatePlayheadState

	// Move after end
	loop.updatePlayheadState(600);
	CHECK_TRUE(loop.isPlayheadInside()); // Still true - only cleared when moving before start

	// Move before start - this clears the flag
	loop.updatePlayheadState(50);
	CHECK_FALSE(loop.isPlayheadInside());

	// Move back into loop
	loop.updatePlayheadState(200);
	CHECK_TRUE(loop.isPlayheadInside());
}

TEST(ArrangementLoopTests, PlayheadStateWithNonExistentLoop) {
	// Playhead state should always be false when loop doesn't exist
	CHECK_FALSE(loop.isPlayheadInside());

	loop.initializePlayheadState(300);
	CHECK_FALSE(loop.isPlayheadInside());

	loop.updatePlayheadState(400);
	CHECK_FALSE(loop.isPlayheadInside());
}

TEST(ArrangementLoopTests, BoundaryConditions) {
	loop.create(100, 500);
	// Loop is auto-activated, but playhead needs to be inside first

	// Set playhead as inside the loop first
	loop.initializePlayheadState(300);
	CHECK_TRUE(loop.isPlayheadInside());

	// Test exact boundary positions for shouldLoopAtPosition
	CHECK_FALSE(loop.shouldLoopAtPosition(99));  // Just before start
	CHECK_FALSE(loop.shouldLoopAtPosition(100)); // At start
	CHECK_FALSE(loop.shouldLoopAtPosition(499)); // Just before end
	CHECK_TRUE(loop.shouldLoopAtPosition(500));  // At end (should loop)
	CHECK_TRUE(loop.shouldLoopAtPosition(501));  // Just after end (should loop)

	// Test playhead state at boundaries
	loop.initializePlayheadState(99);
	CHECK_FALSE(loop.isPlayheadInside());

	loop.initializePlayheadState(100);
	CHECK_TRUE(loop.isPlayheadInside());

	loop.initializePlayheadState(499);
	CHECK_TRUE(loop.isPlayheadInside());

	loop.initializePlayheadState(500);
	CHECK_FALSE(loop.isPlayheadInside());
}

TEST(ArrangementLoopTests, LoopTransitions) {
	loop.create(100, 500);
	// Loop is auto-activated
	loop.initializePlayheadState(300); // Start inside
	CHECK_TRUE(loop.isPlayheadInside());

	// Simulate loop transition - playhead jumps from end to start
	loop.updatePlayheadState(500);       // At end, updatePlayheadState doesn't clear flag
	CHECK_TRUE(loop.isPlayheadInside()); // Still inside according to updatePlayheadState logic

	loop.updatePlayheadState(100);       // Jumped to start
	CHECK_TRUE(loop.isPlayheadInside()); // Still inside
}

TEST(ArrangementLoopTests, MultipleLoopCreations) {
	// Create first loop
	loop.create(100, 300);
	CHECK_EQUAL(100, loop.getStart());
	CHECK_EQUAL(300, loop.getEnd());

	// Create new loop (should replace old one)
	loop.create(200, 600);
	CHECK_EQUAL(200, loop.getStart());
	CHECK_EQUAL(600, loop.getEnd());
	CHECK_EQUAL(400, loop.getLength());
}

TEST(ArrangementLoopTests, StatePreservationAcrossOperations) {
	loop.create(100, 500);
	// Loop is auto-activated
	loop.initializePlayheadState(300);

	CHECK_TRUE(loop.exists());
	CHECK_TRUE(loop.isActive());
	CHECK_TRUE(loop.isPlayheadInside());

	// Operations that shouldn't affect state
	CHECK_TRUE(loop.containsPosition(200));
	CHECK_FALSE(loop.shouldLoopAtPosition(400));

	// State should be preserved
	CHECK_TRUE(loop.exists());
	CHECK_TRUE(loop.isActive());
	CHECK_TRUE(loop.isPlayheadInside());
}
