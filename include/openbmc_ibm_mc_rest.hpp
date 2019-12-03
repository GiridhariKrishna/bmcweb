#pragma once
#include <app.h>
#include <tinyxml2.h>

#include <IBM/locks.hpp>
#include <async_resp.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/container/flat_set.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#include <sdbusplus/message/types.hpp>
#include <utils/json_utils.hpp>

#define MAX_SAVE_AREA_FILESIZE 20000
using stype = std::string;
using segmentflags = std::vector<std::pair<std::string, uint32_t>>;
using lockrecord = std::tuple<stype, stype, stype, uint64_t, segmentflags>;
using lockrequest = std::vector<lockrecord>;
using rc = std::pair<bool, std::variant<uint32_t, lockrecord>>;
using rcgetlocklist = std::pair<
    bool,
    std::variant<std::string, std::vector<std::pair<uint32_t, lockrequest>>>>;

namespace crow
{
namespace openbmc_ibm_mc
{
constexpr const char *methodNotAllowedMsg = "Method Not Allowed";
constexpr const char *resourceNotFoundMsg = "Resource Not Found";
constexpr const char *contentNotAcceptableMsg = "Content Not Acceptable";
constexpr const char *internalServerError = "Internal Server Error";

bool createSaveAreaPath(crow::Response &res)
{
    // The path /var/lib/obmc will be created by initrdscripts
    // Create the directories for the save-area files, when we get
    // first file upload request
    std::error_code ec;
    if (!std::filesystem::is_directory("/var/lib/obmc/bmc-console-mgmt"))
    {
        std::filesystem::create_directory("/var/lib/obmc/bmc-console-mgmt", ec);
    }
    if (ec)
    {
        res.result(boost::beast::http::status::internal_server_error);
        res.jsonValue["Description"] = internalServerError;
        BMCWEB_LOG_DEBUG
            << "handleIbmPost: Failed to prepare save-area directory. ec : "
            << ec;
        return false;
    }

    if (!std::filesystem::is_directory(
            "/var/lib/obmc/bmc-console-mgmt/save-area"))
    {
        std::filesystem::create_directory(
            "/var/lib/obmc/bmc-console-mgmt/save-area", ec);
    }
    if (ec)
    {
        res.result(boost::beast::http::status::internal_server_error);
        res.jsonValue["Description"] = internalServerError;
        BMCWEB_LOG_DEBUG
            << "handleIbmPost: Failed to prepare save-area directory. ec : "
            << ec;
        return false;
    }
    return true;
}
void handleFilePut(const crow::Request &req, crow::Response &res,
                   const std::string &fileID)
{
    // Check the content-type of the request
    std::string_view contentType = req.getHeaderValue("content-type");
    if (boost::starts_with(contentType, "multipart/form-data"))
    {
        BMCWEB_LOG_DEBUG
            << "This is multipart/form-data. Invalid content for PUT";

        res.result(boost::beast::http::status::not_acceptable);
        res.jsonValue["Description"] = contentNotAcceptableMsg;
        return;
    }
    else
    {
        BMCWEB_LOG_DEBUG << "Not a multipart/form-data. Continue..";
    }

    BMCWEB_LOG_DEBUG
        << "handleIbmPut: Request to create/update the save-area file";
    if (!createSaveAreaPath(res))
    {
        res.result(boost::beast::http::status::not_found);
        res.jsonValue["Description"] = resourceNotFoundMsg;
        return;
    }
    // Create the file
    std::ofstream file;
    std::filesystem::path loc("/var/lib/obmc/bmc-console-mgmt/save-area");
    loc /= fileID;

    std::string data = std::move(req.body);
    BMCWEB_LOG_DEBUG << "data capaticty : " << data.capacity();
    if (data.capacity() > MAX_SAVE_AREA_FILESIZE)
    {
        res.result(boost::beast::http::status::bad_request);
        res.jsonValue["Description"] =
            "File size exceeds 200KB. Maximum allowed size is 200KB";
        return;
    }
    BMCWEB_LOG_DEBUG << "Creating file " << loc;
    file.open(loc, std::ofstream::out);
    if (file.fail())
    {
        BMCWEB_LOG_DEBUG << "Error while opening the file for writing";
        res.result(boost::beast::http::status::internal_server_error);
        res.jsonValue["Description"] = "Error while creating the file";
        return;
    }
    else
    {
        file << data;
        BMCWEB_LOG_DEBUG << "save-area file is created";
        res.jsonValue["Description"] = "File Created";
    }
}

void handleConfigFileList(crow::Response &res)
{
    std::vector<std::string> pathObjList;
    std::filesystem::path loc("/var/lib/obmc/bmc-console-mgmt/save-area");
    if (std::filesystem::exists(loc) && std::filesystem::is_directory(loc))
    {
        for (const auto &file : std::filesystem::directory_iterator(loc))
        {
            std::filesystem::path pathObj(file.path());
            pathObjList.push_back("/ibm/v1/Host/ConfigFiles/" +
                                  pathObj.filename().string());
        }
    }
    res.jsonValue["@odata.type"] = "#FileCollection.v1_0_0.FileCollection";
    res.jsonValue["@odata.id"] = "/ibm/v1/Host/ConfigFiles/";
    res.jsonValue["@odata.context"] =
        "/ibm/v1/$metadata#FileCollection.FileCollection";
    res.jsonValue["Id"] = "ConfigFiles";
    res.jsonValue["Name"] = "ConfigFiles";

    res.jsonValue["Members"] = std::move(pathObjList);
    res.jsonValue["Actions"]["#FileCollection.DeleteAll"] = {
        {"target",
         "/ibm/v1/Host/ConfigFiles/Actions/FileCollection.DeleteAll"}};
    res.end();
}

void deleteConfigFiles(crow::Response &res)
{
    std::vector<std::string> pathObjList;
    std::error_code ec;
    std::filesystem::path loc("/var/lib/obmc/bmc-console-mgmt/save-area");
    if (std::filesystem::exists(loc) && std::filesystem::is_directory(loc))
    {
        std::filesystem::remove_all(loc, ec);
        if (ec)
        {
            res.result(boost::beast::http::status::internal_server_error);
            res.jsonValue["Description"] = internalServerError;
            BMCWEB_LOG_DEBUG << "deleteConfigFiles: Failed to delete the "
                                "config files directory. ec : "
                             << ec;
        }
    }
    res.end();
}

void getLockServiceData(crow::Response &res)
{
    res.jsonValue["@odata.type"] = "#LockService.v1_0_0.LockService";
    res.jsonValue["@odata.id"] = "/ibm/v1/HMC/LockService/";
    res.jsonValue["@odata.context"] =
        "/ibm/v1/$metadata#LockService.LockService";
    res.jsonValue["Id"] = "LockService";
    res.jsonValue["Name"] = "LockService";

    res.jsonValue["Actions"]["#LockService.AcquireLock"] = {
        {"target", "/ibm/v1/HMC/LockService/Actions/LockService.AcquireLock"}};
    res.jsonValue["Actions"]["#LockService.ReleaseLock"] = {
        {"target", "/ibm/v1/HMC/LockService/Actions/LockService.ReleaseLock"}};
    res.jsonValue["Actions"]["#LockService.GetLockList"] = {
        {"target", "/ibm/v1/HMC/LockService/Actions/LockService.GetLockList"}};
    res.end();
}

void handleFileGet(crow::Response &res, const std::string &fileID)
{
    BMCWEB_LOG_DEBUG << "HandleGet on SaveArea files on path: " << fileID;
    std::filesystem::path loc("/var/lib/obmc/bmc-console-mgmt/save-area/" +
                              fileID);
    if (!std::filesystem::exists(loc))
    {
        BMCWEB_LOG_ERROR << loc << "Not found";
        res.result(boost::beast::http::status::not_found);
        res.jsonValue["Description"] = resourceNotFoundMsg;
        return;
    }

    std::ifstream readfile(loc.string());
    if (!readfile)
    {
        BMCWEB_LOG_ERROR << loc.string() << "Not found";
        res.result(boost::beast::http::status::not_found);
        res.jsonValue["Description"] = resourceNotFoundMsg;
        return;
    }

    std::string contentDispositionParam =
        "attachment; filename=\"" + fileID + "\"";
    res.addHeader("Content-Disposition", contentDispositionParam);
    std::string fileData;
    fileData = {std::istreambuf_iterator<char>(readfile),
                std::istreambuf_iterator<char>()};
    res.jsonValue["Data"] = fileData;
    return;
}

void handleFileDelete(crow::Response &res, const std::string &fileID)
{
    std::string filePath("/var/lib/obmc/bmc-console-mgmt/save-area/" + fileID);
    BMCWEB_LOG_DEBUG << "Removing the file : " << filePath << "\n";

    std::ifstream file_open(filePath.c_str());
    if (static_cast<bool>(file_open))
        if (remove(filePath.c_str()) == 0)
        {
            BMCWEB_LOG_DEBUG << "File removed!\n";
            res.jsonValue["Description"] = "File Deleted";
        }
        else
        {
            BMCWEB_LOG_ERROR << "File not removed!\n";
            res.result(boost::beast::http::status::internal_server_error);
            res.jsonValue["Description"] = internalServerError;
        }
    else
    {
        BMCWEB_LOG_ERROR << "File not found!\n";
        res.result(boost::beast::http::status::not_found);
        res.jsonValue["Description"] = resourceNotFoundMsg;
    }
    return;
}

inline void handleFileUrl(const crow::Request &req, crow::Response &res,
                          const std::string &fileID)
{
    if (req.method() == "PUT"_method)
    {
        handleFilePut(req, res, fileID);
        res.end();
        return;
    }
    if (req.method() == "GET"_method)
    {
        handleFileGet(res, fileID);
        res.end();
        return;
    }
    if (req.method() == "DELETE"_method)
    {
        handleFileDelete(res, fileID);
        res.end();
        return;
    }
}

template <typename... Middlewares> void requestRoutes(Crow<Middlewares...> &app)
{

    // allowed only for admin
    BMCWEB_ROUTE(app, "/ibm/v1/")
        .requires({"ConfigureComponents", "ConfigureManager"})
        .methods(
            "GET"_method)([](const crow::Request &req, crow::Response &res) {
            res.jsonValue["@odata.type"] = "#ServiceRoot.v1_0_0.ServiceRoot";
            res.jsonValue["@odata.id"] = "/ibm/v1/";
            res.jsonValue["@odata.context"] =
                "/ibm/v1/$metadata#ServiceRoot.ServiceRoot";
            res.jsonValue["Id"] = "IBM Rest RootService";
            res.jsonValue["Name"] = "IBM Rest Root Service";
            res.jsonValue["ConfigFiles"] = {
                {"@odata.id", "/ibm/v1/Host/ConfigFiles"}};
            res.jsonValue["LockService"] = {
                {"@odata.id", "/redfish/v1/HMC/LockService"}};
            res.end();
        });

    BMCWEB_ROUTE(app, "/ibm/v1/Host/ConfigFiles")
        .requires({"ConfigureComponents", "ConfigureManager"})
        .methods("GET"_method)(
            [](const crow::Request &req, crow::Response &res) {
                handleConfigFileList(res);
            });

    BMCWEB_ROUTE(app,
                 "/ibm/v1/Host/ConfigFiles/Actions/FileCollection.DeleteAll")
        .requires({"ConfigureComponents", "ConfigureManager"})
        .methods("POST"_method)(
            [](const crow::Request &req, crow::Response &res) {
                deleteConfigFiles(res);
            });

    BMCWEB_ROUTE(app, "/ibm/v1/Host/ConfigFiles/<path>")
        .requires({"ConfigureComponents", "ConfigureManager"})
        .methods("PUT"_method, "GET"_method, "DELETE"_method)(
            [](const crow::Request &req, crow::Response &res,
               const std::string &path) { handleFileUrl(req, res, path); });

    BMCWEB_ROUTE(app, "/ibm/v1/HMC/LockService")
        .requires({"ConfigureComponents", "ConfigureManager"})
        .methods("GET"_method)(
            [](const crow::Request &req, crow::Response &res) {
                getLockServiceData(res);
            });

    BMCWEB_ROUTE(app, "/ibm/v1/HMC/LockService/Actions/LockService.AcquireLock")
        .requires({"ConfigureComponents", "ConfigureManager"})
        .methods(
            "POST"_method)([](const crow::Request &req, crow::Response &res) {
            std::vector<nlohmann::json> body;

            if (!redfish::json_util::readJson(req, res, "Request", body))
            {
                return;
            }
            BMCWEB_LOG_DEBUG << body.size();
            BMCWEB_LOG_DEBUG << "Data is present";

            lockrequest lockrequeststructure;
            for (auto element : body)
            {
                std::string locktype;
                uint64_t resourceid;

                segmentflags seginfo;
                std::vector<nlohmann::json> segmentflags;

                if (!redfish::json_util::readJson(
                        element, res, "LockType", locktype, "ResourceID",
                        resourceid, "SegmentFlags", segmentflags))
                {
                    return;
                }
                BMCWEB_LOG_DEBUG << locktype;
                BMCWEB_LOG_DEBUG << resourceid;

                BMCWEB_LOG_DEBUG << "Segment Flags are present";

                for (auto e : segmentflags)
                {
                    std::string lockflags;
                    uint32_t segmentlength;

                    if (!redfish::json_util::readJson(
                            e, res, "LockFlag", lockflags, "SegmentLength",
                            segmentlength))
                    {
                        return;
                    }

                    BMCWEB_LOG_DEBUG << "Lockflag : " << lockflags;
                    BMCWEB_LOG_DEBUG << "SegmentLength : " << segmentlength;

                    seginfo.push_back(std::make_pair(lockflags, segmentlength));
                }
                lockrequeststructure.push_back(make_tuple(req.session->uniqueId,
                                                          "hmc-id", locktype,
                                                          resourceid, seginfo));
            }

            // print lock request

            for (uint32_t i = 0; i < lockrequeststructure.size(); i++)
            {
                BMCWEB_LOG_DEBUG << std::get<0>(lockrequeststructure[i]);
                BMCWEB_LOG_DEBUG << std::get<1>(lockrequeststructure[i]);
                BMCWEB_LOG_DEBUG << std::get<2>(lockrequeststructure[i]);
                BMCWEB_LOG_DEBUG << std::get<3>(lockrequeststructure[i]);

                for (const auto &p : std::get<4>(lockrequeststructure[i]))
                {
                    BMCWEB_LOG_DEBUG << p.first << ", " << p.second;
                    // std::cout << std::get<0>(p) << ", " << std::get<1>(p) <<
                    // std::endl;
                }
            }

            // Validate the lock record

            for (uint32_t i = 0; i < lockrequeststructure.size(); i++)
            {
                bool status = crow::ibm_mc_lock::lockobject.isvalidlockrecord(
                    &lockrequeststructure[i]);

                if (!status)
                {
                    BMCWEB_LOG_DEBUG << "Not a Valid record";
                    BMCWEB_LOG_DEBUG << "Bad json in request";
                    res.result(boost::beast::http::status::bad_request);
                    res.end();
                    return;
                }
            }

            // check for conflict record

            bool status = crow::ibm_mc_lock::lockobject.isconflictrequest(
                &lockrequeststructure);

            if (status)
            {
                BMCWEB_LOG_DEBUG << "There is a conflict within itself";
                res.result(boost::beast::http::status::bad_request);
                res.end();
                return;
            }
            else
            {
                BMCWEB_LOG_DEBUG
                    << "The request is not conflicting within itself";

                // Need to check for conflict with the locktable entries.

                auto status = crow::ibm_mc_lock::lockobject.isconflictwithtable(
                    &lockrequeststructure);

                // print my map

                // lockobject.printmymap();

                if (!status.first)
                {
                    BMCWEB_LOG_DEBUG
                        << "There is no conflict with the locktable";
                    res.result(boost::beast::http::status::ok);

                    // nlohmann::json myarray = nlohmann::json::array();
                    auto var = std::get<uint32_t>(status.second);
                    // for (uint32_t i = 0; i < var.size(); i++)
                    //{
                    nlohmann::json returnjson;
                    returnjson["id"] = var;
                    // myarray.push_back(returnjson);
                    //}
                    res.jsonValue["TransactionID"] = var;
                    res.end();
                    return;
                }
                else
                {
                    BMCWEB_LOG_DEBUG
                        << "There is a conflict with the lock table";
                    res.result(boost::beast::http::status::conflict);
                    auto var = std::get<lockrecord>(status.second);
                    nlohmann::json returnjson, segments;
                    nlohmann::json myarray = nlohmann::json::array();
                    returnjson["SessionID"] = std::get<0>(var);
                    returnjson["HMCID"] = std::get<1>(var);
                    returnjson["LockType"] = std::get<2>(var);
                    returnjson["ResourceID"] = std::get<3>(var);

                    for (uint32_t i = 0; i < std::get<4>(var).size(); i++)
                    {
                        segments["LockFlag"] = std::get<4>(var)[i].first;
                        segments["SegmentLength"] = std::get<4>(var)[i].second;
                        myarray.push_back(segments);
                    }

                    returnjson["SegmentFlags"] = myarray;

                    res.jsonValue["Record"] = returnjson;
                    res.end();
                    return;
                }
            }
        });
}

} // namespace openbmc_ibm_mc
} // namespace crow
