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

TEST(ArrangementLoopTests, WrapAroundBeyondLoopEnd) {
	// Test wrap-around handling when position is far beyond loop end
	loop.create(100, 500);
	loop.initializePlayheadState(300); // Set playhead inside loop

	// Test positions well beyond the loop end
	CHECK_TRUE(loop.shouldLoopAtPosition(501));        // Just beyond end
	CHECK_TRUE(loop.shouldLoopAtPosition(600));        // Moderately beyond
	CHECK_TRUE(loop.shouldLoopAtPosition(1000));       // Far beyond end
	CHECK_TRUE(loop.shouldLoopAtPosition(2147483646)); // Near max int32

	// State should remain consistent
	CHECK_TRUE(loop.isPlayheadInside());
	CHECK_TRUE(loop.isActive());
}

TEST(ArrangementLoopTests, WrapAroundCalculationEdgeCases) {
	// Test complex wrap-around scenarios with different loop sizes
	loop.create(1000, 1100);            // Small 100-tick loop
	loop.initializePlayheadState(1050); // Set playhead inside loop

	auto checkWrapAround = [&](int32_t currentPos) -> int32_t {
		if (loop.shouldLoopAtPosition(currentPos)) {
			int32_t loopLength = loop.getEnd() - loop.getStart(); // 100
			int32_t beyondEnd = currentPos - loop.getEnd();       // How far past 1100
			int32_t offsetWithinLoop = beyondEnd % loopLength;
			return loop.getStart() + offsetWithinLoop; // 1000 + offset
		}
		return currentPos;
	};

	// Test small offsets
	CHECK_EQUAL(1000, checkWrapAround(1100)); // Exactly at end → 0 offset → 1000
	CHECK_EQUAL(1001, checkWrapAround(1101)); // 1 beyond → offset 1 → 1001
	CHECK_EQUAL(1050, checkWrapAround(1150)); // 50 beyond → offset 50 → 1050
	CHECK_EQUAL(1099, checkWrapAround(1199)); // 99 beyond → offset 99 → 1099

	// Test complete loop cycles
	CHECK_EQUAL(1000, checkWrapAround(1200)); // 100 beyond → offset 0 → 1000
	CHECK_EQUAL(1025, checkWrapAround(1325)); // 225 beyond → 225%100=25 → 1025
	CHECK_EQUAL(1000, checkWrapAround(1500)); // 400 beyond → 400%100=0 → 1000

	// Test very large positions
	CHECK_EQUAL(1037, checkWrapAround(2137)); // 1037 beyond → 1037%100=37 → 1037
}

TEST(ArrangementLoopTests, PositionResetFromBeyondEnd) {
	// Test that checkForLoopAndGetNewPosition correctly resets from beyond end
	loop.create(100, 500);
	loop.initializePlayheadState(300); // Set playhead inside loop

	// Simulate the arrangement's checkForLoopAndGetNewPosition logic with proper wrap-around
	auto checkPosition = [&](int32_t currentPos) -> int32_t {
		loop.updatePlayheadState(currentPos);
		if (loop.shouldLoopAtPosition(currentPos)) {
			// Calculate how far beyond the loop end we are
			int32_t loopLength = loop.getEnd() - loop.getStart(); // 400
			int32_t beyondEnd = currentPos - loop.getEnd();

			// Handle wrap-around: calculate new offset after start
			int32_t offsetWithinLoop = beyondEnd % loopLength;

			return loop.getStart() + offsetWithinLoop;
		}
		return currentPos;
	};

	// Test various positions beyond the end with proper offset calculation
	CHECK_EQUAL(100, checkPosition(500)); // At end (500) → offset 0 → start + 0 = 100
	CHECK_EQUAL(105, checkPosition(505)); // 5 beyond end → offset 5 → start + 5 = 105
	CHECK_EQUAL(150, checkPosition(550)); // 50 beyond end → offset 50 → start + 50 = 150
	CHECK_EQUAL(200, checkPosition(600)); // 100 beyond end → offset 100 → start + 100 = 200
	CHECK_EQUAL(150, checkPosition(950)); // 450 beyond end → 450 % 400 = 50 → start + 50 = 150
	CHECK_EQUAL(100, checkPosition(900)); // 400 beyond end → 400 % 400 = 0 → start + 0 = 100

	// Test position within loop (should not reset)
	CHECK_EQUAL(300, checkPosition(300)); // Inside loop → no change
	CHECK_EQUAL(450, checkPosition(450)); // Inside loop → no change
}
