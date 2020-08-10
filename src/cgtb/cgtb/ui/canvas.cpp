#include "canvas.h"

#include <GL/glew.h>
#include <iostream>

#include "../third-party/nanovg.h"

#include "element.h"

void cgtb::ui::canvas::begin() {

	// If we're intending to clear the screen then all previous element renders are invalid so clear them.
	if (clear) finalized.clear();
	
	// Reset proposed elements to one, empty layer to get ready for emit()'s.
	proposed.resize(1);
	proposed[0].clear();
}

bool cgtb::ui::canvas::end() {

	// The return-value of this function.
	bool need_swap = false;

	glViewport(0, 0, size.x, size.y);

	if (clear) {
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		need_swap = true;
	}

	// For all elements, update their states and then call their poll().
	for (int layer = 0; layer < proposed.size(); layer++) {
		for (auto &E : proposed[layer]) {
			E.second.hover = cursor_enabled && E.second.body.contains(cursor);
			if (cursor_enabled) E.second.cursor = { cursor.x - E.second.body.x1, cursor.y - E.second.body.y1 };
			auto response = E.second.poll(E.second);
			if (response == action::redraw) dirty.push_back({ layer, E.second.body });
			else if (response == action::redraw_deep) dirty.push_back({ 0, E.second.body });
		}
	}

	nvgBeginFrame(nvgc, size.x, size.y, 1);

	// TODO: 'dirty' areas that intersect with eachother could cause render artifacts. Specifically, problems with alpha-enabled elemements.

	// Iterate all dirty sections of the screen and re-draw all elements involved with that area.

	for (auto &dirty_area : dirty) {

		std::cout << "dirty screen area: " << dirty_area.second.x1 << ", " << dirty_area.second.y1
			<< " -> " << dirty_area.second.x2 << ", " << dirty_area.second.y2 << " ("
			<< ((dirty_area.second.x2 - dirty_area.second.x1) * (dirty_area.second.y2 - dirty_area.second.y1)) << " pixels) -- layers " << dirty_area.first << " to " << proposed.size() << std::endl;

		// Clip all pixels that are outside of the current dirty area.
		nvgScissor(nvgc, dirty_area.second.x1, dirty_area.second.y1, dirty_area.second.x2 - dirty_area.second.x1, dirty_area.second.y2 - dirty_area.second.y1);
				
		// Start at the layer indicated and move upwards.
		// Re-drawing every elements that's within the intended area.
		for (int layer = dirty_area.first; layer < proposed.size(); layer++) {
			for (auto &E : proposed[layer]) {
				if (!E.second.body.intersecting(dirty_area.second)) continue;
				nvgSave(nvgc);
				std::clog << " render '" << E.first << "'" << std::endl;
				E.second.render(nvgc, E.second);
				nvgRestore(nvgc);
				need_swap = true;
			}
		}

		// Disable clipping.
		nvgResetScissor(nvgc);
	}

	nvgEndFrame(nvgc);

	// All dirty areas have been taken care of; so clear it.
	dirty.clear();

	// Copy all of the elements so we can reference them next frame.
	finalized = proposed;

	// The target has already been cleared so don't do it again.
	clear = false;

	// Return true if something changed.
	return need_swap;
}


int cgtb::ui::canvas::emit(const std::string_view &uuid, const area &body, std::function<action(const element &)> poll, std::function<void(NVGcontext *, const element &)> render) {
			
	element tmp;

	tmp.body = body;
	tmp.poll = poll;
	tmp.render = render;

	// Used to keep track of whether an element existed previously, or is new.
	// And if it has transferred layers or not.
	int to_layer = -1, prev_layer = -1;

	// Determine if this UUID has existed previously, and on what layer.
	for (int layer = 0; layer < finalized.size(); layer++) {
		if (finalized[layer].find(uuid.data()) == finalized[layer].end()) continue;
		prev_layer = layer;
		break;
	}

	// Iterate through all the layers and find the earliest where there's space to fit this one.
	for (int layer = 0; layer < proposed.size(); layer++) {
		bool intersecting = false;
		for (auto &other : proposed[layer]) {
			if (other.second.body.intersecting(body)) {
				intersecting = true;
				break;
			}
		}
		if (!intersecting) {
			to_layer = layer;
			break;
		}
	}

	if (to_layer == -1) {
		// If 'to_layer' is still '-1' then there was no room on the existing layers so we need to add a new one for it.
		proposed.push_back({});
		to_layer = proposed.size() - 1;
	}

	// This portion of code determines which areas of the screen need to be updated by checking if the element has moved.
	if (prev_layer == -1) {

		// If this UUID has not existed previously then it must be a new element.
		// Mark the area as dirty so it will be rendered.
		// std::cout << "new element; " << uuid << " @ " << to_layer << std::endl;
		dirty.push_back({ 0, body });

	} else {

		// This UUID has existed previously. Reference the previous instance.
		auto &prev_inst = finalized[prev_layer][uuid.data()];
		tmp.previous = &prev_inst;

		if (prev_inst.body != body) {

			// If the body area is different from previously then the element has moved.
			// We need to mark the old area and it's new area as dirty so everything gets re-drawn properly.
			dirty.push_back({ 0, body });
			dirty.push_back({ 0, prev_inst.body });
			std::cout << "moved element; " << uuid << " @ " << to_layer << " from " << prev_layer << std::endl;

		} else if (prev_layer != to_layer) {

			// The body area is in the same location, but the element has changed to a different layer.
			// Mark whichever layer is lowest as dirty.

			dirty.push_back({ std::min(std::min(prev_layer, to_layer), 0), body });
			std::cout << "moved element (on layer); " << uuid << " @ " << to_layer << " from " << prev_layer << std::endl;
		}
	}

	proposed[to_layer][uuid.data()] = tmp;

	// Return the layer at which the element exists.
	return to_layer;
}