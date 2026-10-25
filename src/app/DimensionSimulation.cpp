#include "app/DimensionSimulation.h"

#include "entity/EntityPresenter.h"

// EntityPresenter is intentionally forward-declared in the header: only this
// out-of-line destructor needs the complete type for unique_ptr ownership.
DimensionSimulation::~DimensionSimulation() = default;
