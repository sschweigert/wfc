#include <wfc/configuru.h>

#include <algorithm>
#include <unordered_set>

#include <emilib/strprintf.hpp>
#include <stb_image.h>


Tile rotate(const Tile& in_tile, const size_t tile_size)
{
	CHECK_EQ_F(in_tile.size(), tile_size * tile_size);
	Tile out_tile;
	for (size_t y : irange(tile_size)) 
	{
		for (size_t x : irange(tile_size)) 
		{
			out_tile.push_back(in_tile[tile_size - 1 - y + x * tile_size]);
		}
	}
	return out_tile;
}

// TODO: Seems like part of the algorithm is here - should be moved to algorithm-specific files. This file is just meant
// for loading config with configuru.
TileModelInternal fromConfig(const TileModelConfig& config)
{
	TileModelInternal toReturn;

	toReturn.mCommonParams.mOutsideCommonParams = config.commonParam;

	// Tile model does not support _foundation. Variable should be removed
	toReturn.mCommonParams._foundation = std::experimental::optional<size_t>();

	toReturn._tile_size        = config.config.get_or("tile_size", 16);
	const bool unique = config.config.get_or("unique",    false);

	std::unordered_set<std::string> subset;
	if (config.subset_name != "") 
	{
		for (const auto& tile_name : config.config["subsets"][config.subset_name].as_array()) 
		{
			subset.insert(tile_name.as_string());
		}
	}

	std::vector<std::array<int,     8>>  action;
	std::unordered_map<std::string, size_t> first_occurrence;

	for (const auto& tile : config.config["tiles"].as_array()) 
	{
		const std::string tile_name = tile["name"].as_string();
		if (!subset.empty() && subset.count(tile_name) == 0) { continue; }

		std::function<int(int)> a, b;
		int cardinality;

		std::string sym = tile.get_or("symmetry", "X");
		if (sym == "L") 
		{
			cardinality = 4;
			a = [](int i){ return (i + 1) % 4; };
			b = [](int i){ return i % 2 == 0 ? i + 1 : i - 1; };
		} 
		else if (sym == "T") 
		{
			cardinality = 4;
			a = [](int i){ return (i + 1) % 4; };
			b = [](int i){ return i % 2 == 0 ? i : 4 - i; };
		}
		else if (sym == "I") 
		{
			cardinality = 2;
			a = [](int i){ return 1 - i; };
			b = [](int i){ return i; };
		} 
		else if (sym == "\\") 
		{
			cardinality = 2;
			a = [](int i){ return 1 - i; };
			b = [](int i){ return 1 - i; };
		}
		else if (sym == "X") 
		{
			cardinality = 1;
			a = [](int i){ return i; };
			b = [](int i){ return i; };
		}
		else 
		{
			ABORT_F("Unknown symmetry '%s'", sym.c_str());
		}

		const size_t num_patterns_so_far = action.size();
		first_occurrence[tile_name] = num_patterns_so_far;

		for (int t = 0; t < cardinality; ++t) 
		{
			std::array<int, 8> map;

			map[0] = t;
			map[1] = a(t);
			map[2] = a(a(t));
			map[3] = a(a(a(t)));
			map[4] = b(t);
			map[5] = b(a(t));
			map[6] = b(a(a(t)));
			map[7] = b(a(a(a(t))));

			for (int s = 0; s < 8; ++s) 
			{
				map[s] += num_patterns_so_far;
			}

			action.push_back(map);
		}

		if (unique) 
		{
			for (int t = 0; t < cardinality; ++t) 
			{
				const Tile bitmap = config.tile_loader(emilib::strprintf("%s %d", tile_name.c_str(), t));
				CHECK_EQ_F(bitmap.size(), toReturn._tile_size * toReturn._tile_size);
				toReturn._tiles.push_back(bitmap);
			}
		}
		else 
		{
			const Tile bitmap = config.tile_loader(emilib::strprintf("%s", tile_name.c_str()));
			CHECK_EQ_F(bitmap.size(), toReturn._tile_size * toReturn._tile_size);
			
			toReturn._tiles.push_back(bitmap);
			for (int t = 1; t < cardinality; ++t) 
			{
				toReturn._tiles.push_back(rotate(toReturn._tiles[num_patterns_so_far + t - 1], toReturn._tile_size));
			}
		}

		for (int t = 0; t < cardinality; ++t) 
		{
			toReturn.mCommonParams._pattern_weight.push_back(tile.get_or("weight", 1.0));
		}
	}

	toReturn.mCommonParams._num_patterns = action.size();

	toReturn._propagator = Array3D<Bool>({4, toReturn.mCommonParams._num_patterns, toReturn.mCommonParams._num_patterns}, false);

	for (const auto& neighbor : config.config["neighbors"].as_array()) 
	{
		const auto left  = neighbor["left"];
		const auto right = neighbor["right"];
		CHECK_EQ_F(left.array_size(),  2u);
		CHECK_EQ_F(right.array_size(), 2u);

		const auto left_tile_name = left[0].as_string();
		const auto right_tile_name = right[0].as_string();

		if (!subset.empty() && (subset.count(left_tile_name) == 0 || subset.count(right_tile_name) == 0)) { continue; }

		int L = action[first_occurrence[left_tile_name]][left[1].get<int>()];
		int R = action[first_occurrence[right_tile_name]][right[1].get<int>()];
		int D = action[L][1];
		int U = action[R][1];

		toReturn._propagator[ { 0, L,            R            } ] = true;
		toReturn._propagator[ { 0, action[L][6], action[R][6] } ] = true;
		toReturn._propagator[ { 0, action[R][4], action[L][4] } ] = true;
		toReturn._propagator[ { 0, action[R][2], action[L][2] } ] = true;

		toReturn._propagator[ { 2, D,            U            } ] = true;
		toReturn._propagator[ { 1, action[U][6], action[D][6] } ] = true;
		toReturn._propagator[ { 1, action[D][4], action[U][4] } ] = true;
		toReturn._propagator[ { 1, action[U][2], action[D][2] } ] = true;
	}

	for (int t1 = 0; t1 < toReturn.mCommonParams._num_patterns; ++t1) 
	{
		for (int t2 = 0; t2 < toReturn.mCommonParams._num_patterns; ++t2) 
		{
			toReturn._propagator[ { 2, t1, t2 } ] = toReturn._propagator[ { 0, t2, t1 } ];
			toReturn._propagator[ { 3, t1, t2 } ] = toReturn._propagator[ { 1, t2, t1 } ];
		}
	}
	return toReturn;
}


PalettedImage load_paletted_image(const std::string& path)
{
	ERROR_CONTEXT("loading sample image", path.c_str());
	int width, height, comp;
	RGBA* rgba = reinterpret_cast<RGBA*>(stbi_load(path.c_str(), &width, &height, &comp, 4));
	CHECK_NOTNULL_F(rgba);
	const auto num_pixels = width * height;

	// Fix issues with stbi_load:
	if (comp == 1) 
	{
		// input was greyscale - set alpha:
		for (auto& pixel : emilib::it_range(rgba, rgba + num_pixels)) 
		{
			pixel.a = pixel.r;
		}
	}
	else 
	{
		if (comp == 3)
		{
			for (auto& pixel : emilib::it_range(rgba, rgba + num_pixels)) 
			{
				pixel.a = 255;
			}
		}
		for (auto& pixel : emilib::it_range(rgba, rgba + num_pixels)) 
		{
			if (pixel.a == 0) 
			{
				pixel = RGBA{0,0,0,0};
			}
		}
	}

	std::vector<RGBA> palette;

	Array2D<ColorIndex> data({ width, height });

	auto pixel_idx = 0;
	for (auto dy : irange(height)) 
	{
		for (auto dx : irange(width)) 
		{
			const RGBA color = rgba[pixel_idx];
			const auto color_idx = std::find(palette.begin(), palette.end(), color) - palette.begin();
			if (color_idx == palette.size()) 
			{
				CHECK_LT_F(palette.size(), MAX_COLORS, "Too many colors in image");
				palette.push_back(color);
			}
			data[{ dx, dy }] = color_idx;

			++pixel_idx;
		}
	}

	stbi_image_free(rgba);

	return PalettedImage{
		data, palette
	};
}


OverlappingModelConfig extractOverlappingConfig(const std::string& image_dir, const configuru::Config& config)
{
	const auto image_filename = config["image"].as_string();
	const auto in_path = image_dir + image_filename;

	return
	{
		.sample_image = load_paletted_image(in_path),
		.periodic_in = config.get_or("periodic_in", true),
		.symmetry = (size_t)config.get_or("symmetry", 8),
		.has_foundation = config.get_or("foundation", false),
		.n = config.get_or("n", 3),
		.commonParam =
		{
			.dimension =
			{
				.width = (size_t)config.get_or("width",        48),
				.height = (size_t)config.get_or("height",       48)
			},
			._periodic_out = config.get_or("periodic_out", true)
		}
	};
}

Tile loadTile(const std::string& subdir, const std::string& image_dir, const std::string& tile_name)
{
	const std::string path = emilib::strprintf("%s%s/%s.bmp", image_dir.c_str(), subdir.c_str(), tile_name.c_str());
	int width, height, comp;
	RGBA* rgba = reinterpret_cast<RGBA*>(stbi_load(path.c_str(), &width, &height, &comp, 4));
	CHECK_NOTNULL_F(rgba);
	const auto num_pixels = width * height;
	Tile tile(rgba, rgba + num_pixels);
	stbi_image_free(rgba);
	return tile;
}


TileModelConfig extractConfig(const std::string& image_dir, const configuru::Config& topConfig)
{
	const std::string subdir     = topConfig["subdir"].as_string();

	const auto root_dir = image_dir + subdir + "/";

	return
	{
		.config = configuru::parse_file(root_dir + "data.cfg", configuru::CFG),
		.subset_name = topConfig.get_or("subset",   std::string()),
		.commonParam =
		{
			.dimension =
			{
				.width = (size_t)topConfig.get_or("width",    48),
				.height = (size_t)topConfig.get_or("height",   48)
			},
			._periodic_out = topConfig.get_or("periodic", false)
		},
		.tile_loader = [image_dir, subdir] (const std::string& tile_name)
		{
			return loadTile(subdir, image_dir, tile_name);
		}
	};
}

GeneralConfig importGeneralConfig(const std::string& name, const auto& config)
{
	size_t importedLimit = (size_t)config.get_or("limit", 0);
	std::experimental::optional<size_t> actualLimit;
	if (importedLimit != 0)
	{
		actualLimit = importedLimit;
	}

	return
	{
		.limit       = actualLimit,
		.numOutput = (size_t)config.get_or("numOutput", 2),
		.name = name
	};
}

void run_config_file(const std::string& path, ConfigActions actions)
{
	LOG_F(INFO, "Running all samples in %s", path.c_str());
	const auto samples = configuru::parse_file(path, configuru::CFG);
	const auto image_dir = samples["image_dir"].as_string();

	if (samples.count("overlapping")) 
	{
		for (const auto& p : samples["overlapping"].as_object()) 
		{
			LOG_SCOPE_F(INFO, "%s", p.key().c_str());

			const auto& config = p.value();
			
			GeneralConfig generalConfig = importGeneralConfig(p.key(), config);

			OverlappingModelConfig overlappingModelConfig = extractOverlappingConfig(image_dir, config);
			actions.overlappingAction(generalConfig, overlappingModelConfig);

			p.value().check_dangling();
		}
	}

	if (samples.count("tiled")) 
	{
		for (const auto& p : samples["tiled"].as_object()) 
		{
			LOG_SCOPE_F(INFO, "Tiled %s", p.key().c_str());

			const auto& config = p.value();

			GeneralConfig generalConfig = importGeneralConfig(p.key(), config);

			TileModelConfig tileModelConfig = extractConfig(image_dir, config);
			auto internal = fromConfig(tileModelConfig);

			actions.tileAction(generalConfig, internal);

		}
	}
}
