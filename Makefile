.PHONY: all clean install load unload

BUILD_DIR = build

all:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && make -j$(nproc)
	@echo "Built: $(BUILD_DIR)/hypr-hot-edge.so"

clean:
	rm -rf $(BUILD_DIR)

load: all
	hyprctl plugin load $(PWD)/$(BUILD_DIR)/hypr-hot-edge.so

unload:
	hyprctl plugin unload $(PWD)/$(BUILD_DIR)/hypr-hot-edge.so

# `unload` does not drop the mapping (Hyprland still holds references), so a
# following `load` of the SAME path gets the cached image back from dlopen and
# silently keeps running the OLD code -- it reports "ok" either way. Load a
# uniquely named copy so the linker is forced to map the new build.
reload: all
	-@ls $(BUILD_DIR)/live-*.so 2>/dev/null | xargs -r -I{} hyprctl plugin unload $(PWD)/{}
	@rm -f $(BUILD_DIR)/live-*.so
	@cp $(BUILD_DIR)/hypr-hot-edge.so $(BUILD_DIR)/live-$$(date +%s).so
	@hyprctl plugin load $(PWD)/$$(ls $(BUILD_DIR)/live-*.so)

install: all
	sudo install -Dm755 $(BUILD_DIR)/hypr-hot-edge.so /usr/lib/hyprland/plugins/hypr-hot-edge.so
