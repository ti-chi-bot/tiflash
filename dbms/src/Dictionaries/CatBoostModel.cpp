// Copyright 2023 PingCAP, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <Columns/ColumnFixedString.h>
#include <Columns/ColumnString.h>
#include <Columns/ColumnVector.h>
#include <Common/FieldVisitors.h>
#include <Common/PODArray.h>
#include <Common/SharedLibrary.h>
#include <Common/typeid_cast.h>
#include <Dictionaries/CatBoostModel.h>
#include <IO/Operators.h>
#include <IO/WriteBufferFromString.h>

#include <mutex>

namespace DB
{
namespace ErrorCodes
{
extern const int LOGICAL_ERROR;
extern const int BAD_ARGUMENTS;
extern const int CANNOT_LOAD_CATBOOST_MODEL;
extern const int CANNOT_APPLY_CATBOOST_MODEL;
} // namespace ErrorCodes


/// CatBoost wrapper interface functions.
struct CatBoostWrapperAPI
{
    using ModelCalcerHandle = void;

    ModelCalcerHandle * (*model_calcer_create)();

    void (*model_calcer_delete)(ModelCalcerHandle * calcer);

    const char * (*get_error_string)();

    bool (*load_full_model_from_file)(ModelCalcerHandle * calcer, const char * filename);

    bool (*calc_model_prediction_flat)(ModelCalcerHandle * calcer, size_t docCount, const float ** floatFeatures, size_t floatFeaturesSize, double * result, size_t resultSize);

    bool (*calc_model_prediction)(ModelCalcerHandle * calcer, size_t docCount, const float ** floatFeatures, size_t floatFeaturesSize, const char *** catFeatures, size_t catFeaturesSize, double * result, size_t resultSize);

    bool (*calc_model_prediction_with_hashed_cat_features)(ModelCalcerHandle * calcer, size_t docCount, const float ** floatFeatures, size_t floatFeaturesSize, const int ** catFeatures, size_t catFeaturesSize, double * result, size_t resultSize);

    int (*get_string_cat_feature_hash)(const char * data, size_t size);

    int (*get_integer_cat_feature_hash)(Int64 val);

    size_t (*get_float_features_count)(ModelCalcerHandle * calcer);

    size_t (*get_cat_features_count)(ModelCalcerHandle * calcer);
};


namespace
{
class CatBoostModelHolder
{
private:
    CatBoostWrapperAPI::ModelCalcerHandle * handle;
    const CatBoostWrapperAPI * api;

public:
    explicit CatBoostModelHolder(const CatBoostWrapperAPI * api)
        : api(api)
    {
        handle = api->model_calcer_create();
    }
    ~CatBoostModelHolder() { api->model_calcer_delete(handle); }

    CatBoostWrapperAPI::ModelCalcerHandle * get() { return handle; }
    explicit operator CatBoostWrapperAPI::ModelCalcerHandle *() { return handle; }
};


class CatBoostModelImpl : public ICatBoostModel
{
public:
    CatBoostModelImpl(const CatBoostWrapperAPI * api, const std::string & model_path)
        : api(api)
    {
        auto tmp_handle = std::make_unique<CatBoostModelHolder>(api);
        if (!tmp_handle)
        {
            std::string msg = "Cannot create CatBoost model: ";
            throw Exception(msg + api->get_error_string(), ErrorCodes::CANNOT_LOAD_CATBOOST_MODEL);
        }
        if (!api->load_full_model_from_file(tmp_handle->get(), model_path.c_str()))
        {
            std::string msg = "Cannot load CatBoost model: ";
            throw Exception(msg + api->get_error_string(), ErrorCodes::CANNOT_LOAD_CATBOOST_MODEL);
        }

        float_features_count = api->get_float_features_count(tmp_handle->get());
        cat_features_count = api->get_cat_features_count(tmp_handle->get());

        handle = std::move(tmp_handle);
    }

    ColumnPtr evaluate(const ColumnRawPtrs & columns) const override
    {
        if (columns.empty())
            throw Exception("Got empty columns list for CatBoost model.", ErrorCodes::BAD_ARGUMENTS);

        if (columns.size() != float_features_count + cat_features_count)
        {
            std::string msg;
            {
                WriteBufferFromString buffer(msg);
                buffer << "Number of columns is different with number of features: ";
                buffer << columns.size() << " vs " << float_features_count << " + " << cat_features_count;
            }
            throw Exception(msg, ErrorCodes::BAD_ARGUMENTS);
        }

        for (size_t i = 0; i < float_features_count; ++i)
        {
            if (!columns[i]->isNumeric())
            {
                std::string msg;
                {
                    WriteBufferFromString buffer(msg);
                    buffer << "Column " << i << "should be numeric to make float feature.";
                }
                throw Exception(msg, ErrorCodes::BAD_ARGUMENTS);
            }
        }

        bool cat_features_are_strings = true;
        for (size_t i = float_features_count; i < float_features_count + cat_features_count; ++i)
        {
            const auto * column = columns[i];
            if (column->isNumeric())
                cat_features_are_strings = false;
            else if (!(typeid_cast<const ColumnString *>(column)
                       || typeid_cast<const ColumnFixedString *>(column)))
            {
                std::string msg;
                {
                    WriteBufferFromString buffer(msg);
                    buffer << "Column " << i << "should be numeric or string.";
                }
                throw Exception(msg, ErrorCodes::BAD_ARGUMENTS);
            }
        }

        return evalImpl(columns, float_features_count, cat_features_count, cat_features_are_strings);
    }

    size_t getFloatFeaturesCount() const override { return float_features_count; }
    size_t getCatFeaturesCount() const override { return cat_features_count; }

private:
    std::unique_ptr<CatBoostModelHolder> handle;
    const CatBoostWrapperAPI * api;
    size_t float_features_count;
    size_t cat_features_count;

    /// Buffer should be allocated with features_count * column->size() elements.
    /// Place column elements in positions buffer[0], buffer[features_count], ... , buffer[size * features_count]
    template <typename T>
    void placeColumnAsNumber(const IColumn * column, T * buffer, size_t features_count) const
    {
        size_t size = column->size();
        FieldVisitorConvertToNumber<T> visitor;
        for (size_t i = 0; i < size; ++i)
        {
            /// TODO: Replace with column visitor.
            Field field;
            column->get(i, field);
            *buffer = applyVisitor(visitor, field);
            buffer += features_count;
        }
    }

    /// Buffer should be allocated with features_count * column->size() elements.
    /// Place string pointers in positions buffer[0], buffer[features_count], ... , buffer[size * features_count]
    static void placeStringColumn(const ColumnString & column, const char ** buffer, size_t features_count)
    {
        size_t size = column.size();
        for (size_t i = 0; i < size; ++i)
        {
            *buffer = const_cast<char *>(column.getDataAtWithTerminatingZero(i).data);
            buffer += features_count;
        }
    }

    /// Buffer should be allocated with features_count * column->size() elements.
    /// Place string pointers in positions buffer[0], buffer[features_count], ... , buffer[size * features_count]
    /// Returns PODArray which holds data (because ColumnFixedString doesn't store terminating zero).
    static PODArray<char> placeFixedStringColumn(
        const ColumnFixedString & column,
        const char ** buffer,
        size_t features_count)
    {
        size_t size = column.size();
        size_t str_size = column.getN();
        PODArray<char> data(size * (str_size + 1));
        char * data_ptr = data.data();

        for (size_t i = 0; i < size; ++i)
        {
            auto ref = column.getDataAt(i);
            memcpy(data_ptr, ref.data, ref.size);
            data_ptr[ref.size] = 0;
            *buffer = data_ptr;
            data_ptr += ref.size + 1;
            buffer += features_count;
        }

        return data;
    }

    /// Place columns into buffer, returns column which holds placed data. Buffer should contains column->size() values.
    template <typename T>
    ColumnPtr placeNumericColumns(const ColumnRawPtrs & columns,
                                  size_t offset,
                                  size_t size,
                                  const T ** buffer) const
    {
        if (size == 0)
            return nullptr;
        size_t column_size = columns[offset]->size();
        auto data_column = ColumnVector<T>::create(size * column_size);
        T * data = data_column->getData().data();
        for (size_t i = 0; i < size; ++i)
        {
            const auto * column = columns[offset + i];
            if (column->isNumeric())
                placeColumnAsNumber(column, data + i, size);
        }

        for (size_t i = 0; i < column_size; ++i)
        {
            *buffer = data;
            ++buffer;
            data += size;
        }

        return data_column;
    }

    /// Place columns into buffer, returns data which was used for fixed string columns.
    /// Buffer should contains column->size() values, each value contains size strings.
    static std::vector<PODArray<char>> placeStringColumns(
        const ColumnRawPtrs & columns,
        size_t offset,
        size_t size,
        const char ** buffer)
    {
        if (size == 0)
            return {};

        std::vector<PODArray<char>> data;
        for (size_t i = 0; i < size; ++i)
        {
            const auto * column = columns[offset + i];
            if (const auto * column_string = typeid_cast<const ColumnString *>(column))
                placeStringColumn(*column_string, buffer + i, size);
            else if (const auto * column_fixed_string = typeid_cast<const ColumnFixedString *>(column))
                data.push_back(placeFixedStringColumn(*column_fixed_string, buffer + i, size));
            else
                throw Exception("Cannot place string column.", ErrorCodes::LOGICAL_ERROR);
        }

        return data;
    }

    /// Calc hash for string cat feature at ps positions.
    template <typename Column>
    void calcStringHashes(const Column * column, size_t ps, const int ** buffer) const
    {
        size_t column_size = column->size();
        for (size_t j = 0; j < column_size; ++j)
        {
            auto ref = column->getDataAt(j);
            const_cast<int *>(*buffer)[ps] = api->get_string_cat_feature_hash(ref.data, ref.size);
            ++buffer;
        }
    }

    /// Calc hash for int cat feature at ps position. Buffer at positions ps should contains unhashed values.
    void calcIntHashes(size_t column_size, size_t ps, const int ** buffer) const
    {
        for (size_t j = 0; j < column_size; ++j)
        {
            const_cast<int *>(*buffer)[ps] = api->get_integer_cat_feature_hash((*buffer)[ps]);
            ++buffer;
        }
    }

    /// buffer contains column->size() rows and size columns.
    /// For int cat features calc hash inplace.
    /// For string cat features calc hash from column rows.
    void calcHashes(const ColumnRawPtrs & columns, size_t offset, size_t size, const int ** buffer) const
    {
        if (size == 0)
            return;
        size_t column_size = columns[offset]->size();

        std::vector<PODArray<char>> data;
        for (size_t i = 0; i < size; ++i)
        {
            const auto * column = columns[offset + i];
            if (const auto * column_string = typeid_cast<const ColumnString *>(column))
                calcStringHashes(column_string, i, buffer);
            else if (const auto * column_fixed_string = typeid_cast<const ColumnFixedString *>(column))
                calcStringHashes(column_fixed_string, i, buffer);
            else
                calcIntHashes(column_size, i, buffer);
        }
    }

    /// buffer[column_size * cat_features_count] -> char * => cat_features[column_size][cat_features_count] -> char *
    static void fillCatFeaturesBuffer(const char *** cat_features, const char ** buffer, size_t column_size, size_t cat_features_count)
    {
        for (size_t i = 0; i < column_size; ++i)
        {
            *cat_features = buffer;
            ++cat_features;
            buffer += cat_features_count;
        }
    }

    /// Convert values to row-oriented format and call evaluation function from CatBoost wrapper api.
    ///  * calc_model_prediction_flat if no cat features
    ///  * calc_model_prediction if all cat features are strings
    ///  * calc_model_prediction_with_hashed_cat_features if has int cat features.
    ColumnPtr evalImpl(const ColumnRawPtrs & columns, size_t float_features_count, size_t cat_features_count, bool cat_features_are_strings) const
    {
        std::string error_msg = "Error occurred while applying CatBoost model: ";
        size_t column_size = columns.front()->size();

        auto result = ColumnFloat64::create(column_size);
        auto * result_buf = result->getData().data();

        /// Prepare float features.
        PODArray<const float *> float_features(column_size);
        auto * float_features_buf = float_features.data();
        /// Store all float data into single column. float_features is a list of pointers to it.
        auto float_features_col = placeNumericColumns<float>(columns, 0, float_features_count, float_features_buf);

        if (cat_features_count == 0)
        {
            if (!api->calc_model_prediction_flat(handle->get(), column_size, float_features_buf, float_features_count, result_buf, column_size))
            {
                throw Exception(error_msg + api->get_error_string(), ErrorCodes::CANNOT_APPLY_CATBOOST_MODEL);
            }
            return result;
        }

        /// Prepare cat features.
        if (cat_features_are_strings)
        {
            /// cat_features_holder stores pointers to ColumnString data or fixed_strings_data.
            PODArray<const char *> cat_features_holder(cat_features_count * column_size);
            PODArray<const char **> cat_features(column_size);
            auto * cat_features_buf = cat_features.data();

            fillCatFeaturesBuffer(cat_features_buf, cat_features_holder.data(), column_size, cat_features_count);
            /// Fixed strings are stored without termination zero, so have to copy data into fixed_strings_data.
            auto fixed_strings_data = placeStringColumns(columns, float_features_count, cat_features_count, cat_features_holder.data());

            if (!api->calc_model_prediction(handle->get(), column_size, float_features_buf, float_features_count, cat_features_buf, cat_features_count, result_buf, column_size))
            {
                throw Exception(error_msg + api->get_error_string(), ErrorCodes::CANNOT_APPLY_CATBOOST_MODEL);
            }
        }
        else
        {
            PODArray<const int *> cat_features(column_size);
            auto * cat_features_buf = cat_features.data();
            auto cat_features_col = placeNumericColumns<int>(columns, float_features_count, cat_features_count, cat_features_buf);
            calcHashes(columns, float_features_count, cat_features_count, cat_features_buf);
            if (!api->calc_model_prediction_with_hashed_cat_features(
                    handle->get(),
                    column_size,
                    float_features_buf,
                    float_features_count,
                    cat_features_buf,
                    cat_features_count,
                    result_buf,
                    column_size))
            {
                throw Exception(error_msg + api->get_error_string(), ErrorCodes::CANNOT_APPLY_CATBOOST_MODEL);
            }
        }

        return result;
    }
};


/// Holds CatBoost wrapper library and provides wrapper interface.
class CatBoostLibHolder : public CatBoostWrapperAPIProvider
{
public:
    explicit CatBoostLibHolder(std::string lib_path_)
        : lib_path(std::move(lib_path_))
        , lib(lib_path)
    {
        initAPI();
    }

    const CatBoostWrapperAPI & getAPI() const override { return api; }
    const std::string & getCurrentPath() const { return lib_path; }

private:
    CatBoostWrapperAPI api;
    std::string lib_path;
    SharedLibrary lib;

    void initAPI();

    template <typename T>
    void load(T & func, const std::string & name)
    {
        func = lib.get<T>(name);
    }
};

void CatBoostLibHolder::initAPI()
{
    load(api.model_calcer_create, "ModelCalcerCreate");
    load(api.model_calcer_delete, "ModelCalcerDelete");
    load(api.get_error_string, "GetErrorString");
    load(api.load_full_model_from_file, "LoadFullModelFromFile");
    load(api.calc_model_prediction_flat, "CalcModelPredictionFlat");
    load(api.calc_model_prediction, "CalcModelPrediction");
    load(api.calc_model_prediction_with_hashed_cat_features, "CalcModelPredictionWithHashedCatFeatures");
    load(api.get_string_cat_feature_hash, "GetStringCatFeatureHash");
    load(api.get_integer_cat_feature_hash, "GetIntegerCatFeatureHash");
    load(api.get_float_features_count, "GetFloatFeaturesCount");
    load(api.get_cat_features_count, "GetCatFeaturesCount");
}

std::shared_ptr<CatBoostLibHolder> getCatBoostWrapperHolder(const std::string & lib_path)
{
    static std::weak_ptr<CatBoostLibHolder> ptr;
    static std::mutex mutex;

    std::lock_guard lock(mutex);
    auto result = ptr.lock();

    if (!result || result->getCurrentPath() != lib_path)
    {
        result = std::make_shared<CatBoostLibHolder>(lib_path);
        /// This assignment is not atomic, which prevents from creating lock only inside 'if'.
        ptr = result;
    }

    return result;
}

} // namespace


CatBoostModel::CatBoostModel(std::string name_, std::string model_path_, std::string lib_path_, const ExternalLoadableLifetime & lifetime)
    : name(std::move(name_))
    , model_path(std::move(model_path_))
    , lib_path(std::move(lib_path_))
    , lifetime(lifetime)
{
    try
    {
        init(lib_path);
    }
    catch (...)
    {
        creation_exception = std::current_exception();
    }

    creation_time = std::chrono::system_clock::now();
}

void CatBoostModel::init(const std::string & lib_path)
{
    api_provider = getCatBoostWrapperHolder(lib_path);
    api = &api_provider->getAPI();
    model = std::make_unique<CatBoostModelImpl>(api, model_path);
    float_features_count = model->getFloatFeaturesCount();
    cat_features_count = model->getCatFeaturesCount();
}

const ExternalLoadableLifetime & CatBoostModel::getLifetime() const
{
    return lifetime;
}

bool CatBoostModel::isModified() const
{
    return true;
}

std::unique_ptr<IExternalLoadable> CatBoostModel::clone() const
{
    return std::make_unique<CatBoostModel>(name, model_path, lib_path, lifetime);
}

size_t CatBoostModel::getFloatFeaturesCount() const
{
    return float_features_count;
}

size_t CatBoostModel::getCatFeaturesCount() const
{
    return cat_features_count;
}

ColumnPtr CatBoostModel::evaluate(const ColumnRawPtrs & columns) const
{
    if (!model)
        throw Exception("CatBoost model was not loaded.", ErrorCodes::LOGICAL_ERROR);
    return model->evaluate(columns);
}

} // namespace DB
