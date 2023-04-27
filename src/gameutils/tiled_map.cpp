#include "tiled_map.h"

#include "strings/format.h"
#include "utils/mat.h"

namespace Tiled
{
    Json::View FindLayer(Json::View map, std::string name)
    {
        Json::View ret = FindLayerOpt(map, name);
        if (!ret)
            throw std::runtime_error(FMT("Map layer `{}` is missing.", name));
        return ret;
    }

    Json::View FindLayerOpt(Json::View map, std::string name)
    {
        Json::View ret;

        map["layers"].ForEachArrayElement([&](Json::View elem)
        {
            if (elem["name"].GetString() == name)
            {
                if (!ret)
                    ret = elem;
                else
                    throw std::runtime_error(FMT("More than one layer is named `{}`.", name));
            }
        });

        return ret;
    }

    TileLayer LoadTileLayer(Json::View source)
    {
        if (!source)
            throw std::runtime_error("Attempt to load a null tile layer.");

        if (source["type"].GetString() != "tilelayer")
            throw std::runtime_error(FMT("Expected `{}` to be a tile layer.", source["name"].GetString()));

        ivec2 size(source["width"].GetInt(), source["height"].GetInt());

        Json::View array_view = source["data"];
        if (array_view.GetArraySize() != size.prod())
            throw std::runtime_error(FMT("Expected the layer of size {} to have exactly {} tiles.", size, size.prod()));

        TileLayer ret(size);
        int index = 0;

        for (int y = 0; y < ret.size().y; y++)
        for (int x = 0; x < ret.size().x; x++)
            ret.unsafe_at(ivec2(x,y)) = array_view[index++].GetInt();

        return ret;
    }

    PointLayer LoadPointLayer(Json::View source)
    {
        if (!source)
            throw std::runtime_error("Attempt to load a null point layer.");

        if (source["type"].GetString() != "objectgroup")
            throw std::runtime_error(FMT("Expected `{}` to be an object layer.", source["name"].GetString()));

        PointLayer ret;

        source["objects"].ForEachArrayElement([&](Json::View elem)
        {
            if (!elem.HasElement("point") || elem["point"].GetBool() != true)
                throw std::runtime_error(FMT("Expected every object on layer `{}` to be a point.", source["name"].GetString()));

            ret.points.insert({elem["name"].GetString(), fvec2(elem["x"].GetReal(), elem["y"].GetReal())});
        });

        return ret;
    }

    Properties LoadProperties(Json::View map)
    {
        Properties ret;
        map["properties"].ForEachArrayElement([&](Json::View elem)
        {
            std::string type = elem["type"].GetString();
            if (type == "string")
                ret.strings.insert({elem["name"].GetString(), elem["value"].GetString()});
        });
        return ret;
    }
}
