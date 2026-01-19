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

reload: unload load

install: all
	sudo install -Dm755 $(BUILD_DIR)/hypr-hot-edge.so /usr/lib/hyprland/plugins/hypr-hot-edge.so
