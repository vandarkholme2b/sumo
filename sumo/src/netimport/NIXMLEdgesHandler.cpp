/****************************************************************************/
/// @file    NIXMLEdgesHandler.cpp
/// @author  Daniel Krajzewicz
/// @author  Jakob Erdmann
/// @author  Michael Behrisch
/// @author  Walter Bamberger
/// @author  Laura Bieker
/// @date    Tue, 20 Nov 2001
/// @version $Id$
///
// Importer for network edges stored in XML
/****************************************************************************/
// SUMO, Simulation of Urban MObility; see http://sumo.sourceforge.net/
// Copyright (C) 2001-2012 DLR (http://www.dlr.de/) and contributors
/****************************************************************************/
//
//   This file is part of SUMO.
//   SUMO is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
/****************************************************************************/


// ===========================================================================
// included modules
// ===========================================================================
#ifdef _MSC_VER
#include <windows_config.h>
#else
#include <config.h>
#endif

#include <string>
#include <iostream>
#include <map>
#include <cmath>
#include <xercesc/sax/HandlerBase.hpp>
#include <xercesc/sax/AttributeList.hpp>
#include <xercesc/sax/SAXParseException.hpp>
#include <xercesc/sax/SAXException.hpp>
#include <utils/xml/SUMOSAXHandler.h>
#include <netbuild/NBNodeCont.h>
#include <netbuild/NBTypeCont.h>
#include <utils/xml/SUMOXMLDefinitions.h>
#include <utils/common/MsgHandler.h>
#include <utils/common/TplConvert.h>
#include <utils/common/StringTokenizer.h>
#include <utils/geom/GeomConvHelper.h>
#include <utils/common/ToString.h>
#include <utils/options/OptionsCont.h>
#include <utils/geom/GeoConvHelper.h>
#include "NILoader.h"
#include "NIXMLEdgesHandler.h"

#ifdef CHECK_MEMORY_LEAKS
#include <foreign/nvwa/debug_new.h>
#endif // CHECK_MEMORY_LEAKS


// ===========================================================================
// used constants
// ===========================================================================
const SUMOReal SUMOXML_INVALID_POSITION = -999999.;

// ===========================================================================
// method definitions
// ===========================================================================
NIXMLEdgesHandler::NIXMLEdgesHandler(NBNodeCont& nc,
                                     NBEdgeCont& ec,
                                     NBTypeCont& tc,
                                     NBDistrictCont& dc,
                                     OptionsCont& options)
    : SUMOSAXHandler("xml-edges - file"),
      myOptions(options),
      myNodeCont(nc), myEdgeCont(ec), myTypeCont(tc), myDistrictCont(dc),
      myCurrentEdge(0), myHaveReportedAboutOverwriting(false),
      myHaveWarnedAboutDeprecatedSpreadType(false),
      myHaveWarnedAboutDeprecatedFromTo(false),
      myHaveWarnedAboutDeprecatedNoLanes(false),
      myHaveWarnedAboutDeprecatedLaneId(false) {}


NIXMLEdgesHandler::~NIXMLEdgesHandler() {}


void
NIXMLEdgesHandler::myStartElement(int element,
                                  const SUMOSAXAttributes& attrs) {
    switch (element) {
        case SUMO_TAG_EDGE:
            addEdge(attrs);
            break;
        case SUMO_TAG_LANE:
            addLane(attrs);
            break;
        case SUMO_TAG_SPLIT:
            addSplit(attrs);
            break;
        case SUMO_TAG_DELETE:
            deleteEdge(attrs);
            break;
        default:
            break;
    }
}


void
NIXMLEdgesHandler::addEdge(const SUMOSAXAttributes& attrs) {
    myIsUpdate = false;
    bool ok = true;
    // initialise the edge
    myCurrentEdge = 0;
    mySplits.clear();
    // get the id, report an error if not given or empty...
    myCurrentID = attrs.getStringReporting(SUMO_ATTR_ID, 0, ok);
    if (!ok) {
        return;
    }
    myCurrentEdge = myEdgeCont.retrieve(myCurrentID);
    // check deprecated (unused) attributes
    // use default values, first
    myCurrentSpeed = myTypeCont.getSpeed("");
    myCurrentPriority = myTypeCont.getPriority("");
    myCurrentLaneNo = myTypeCont.getNumLanes("");
    myPermissions = myTypeCont.getPermissions("");
    myCurrentWidth = myTypeCont.getWidth("");
    myCurrentOffset = NBEdge::UNSPECIFIED_OFFSET;
    myCurrentType = "";
    myShape = PositionVector();
    myLanesSpread = LANESPREAD_RIGHT;
    myLength = NBEdge::UNSPECIFIED_LOADED_LENGTH;
    myCurrentStreetName = "";
    // check whether a type's values shall be used
    if (attrs.hasAttribute(SUMO_ATTR_TYPE)) {
        myCurrentType = attrs.getStringReporting(SUMO_ATTR_TYPE, myCurrentID.c_str(), ok);
        if (!ok) {
            return;
        }
        if (!myTypeCont.knows(myCurrentType)) {
            WRITE_ERROR("Type '" + myCurrentType + "' used by edge '" + myCurrentID + "' was not defined.");
            return;
        }
        myCurrentSpeed = myTypeCont.getSpeed(myCurrentType);
        myCurrentPriority = myTypeCont.getPriority(myCurrentType);
        myCurrentLaneNo = myTypeCont.getNumLanes(myCurrentType);
        myPermissions = myTypeCont.getPermissions(myCurrentType);
        myCurrentWidth = myTypeCont.getWidth(myCurrentType);
    }
    // use values from the edge to overwrite if existing, then
    if (myCurrentEdge != 0) {
        myIsUpdate = true;
        if (!myHaveReportedAboutOverwriting) {
            WRITE_MESSAGE("Duplicate edge id occured ('" + myCurrentID + "'); assuming overwriting is wished.");
            myHaveReportedAboutOverwriting = true;
        }
        if (attrs.getOptBoolReporting(SUMO_ATTR_REMOVE, myCurrentID.c_str(), ok, false)) {
            myEdgeCont.erase(myDistrictCont, myCurrentEdge);
            myCurrentEdge = 0;
            return;
        }
        myCurrentSpeed = myCurrentEdge->getSpeed();
        myCurrentPriority = myCurrentEdge->getPriority();
        myCurrentLaneNo = myCurrentEdge->getNumLanes();
        myCurrentType = myCurrentEdge->getTypeID();
        myPermissions = myCurrentEdge->getPermissions();
        if (!myCurrentEdge->hasDefaultGeometry()) {
            myShape = myCurrentEdge->getGeometry();
        }
        myCurrentWidth = myCurrentEdge->getWidth();
        myCurrentOffset = myCurrentEdge->getOffset();
        myLanesSpread = myCurrentEdge->getLaneSpreadFunction();
        if (myCurrentEdge->hasLoadedLength()) {
            myLength = myCurrentEdge->getLoadedLength();
        }
        myCurrentStreetName = myCurrentEdge->getStreetName();
    }
    // speed, priority and the number of lanes have now default values;
    // try to read the real values from the file
    if (attrs.hasAttribute(SUMO_ATTR_SPEED)) {
        myCurrentSpeed = attrs.getSUMORealReporting(SUMO_ATTR_SPEED, myCurrentID.c_str(), ok);
    }
    if (myOptions.getBool("speed-in-kmh")) {
        myCurrentSpeed = myCurrentSpeed / (SUMOReal) 3.6;
    }
    // try to get the number of lanes
    if (attrs.hasAttribute(SUMO_ATTR_NOLANES__DEPRECATED)) {
        myCurrentLaneNo = attrs.getIntReporting(SUMO_ATTR_NOLANES__DEPRECATED, myCurrentID.c_str(), ok);
        if (!myHaveWarnedAboutDeprecatedNoLanes) {
            myHaveWarnedAboutDeprecatedNoLanes = true;
            WRITE_WARNING("'" + toString(SUMO_ATTR_NOLANES__DEPRECATED) + "' is deprecated, please use '" + toString(SUMO_ATTR_NUMLANES) + "' instead.");
        }
    }
    if (attrs.hasAttribute(SUMO_ATTR_NUMLANES)) {
        myCurrentLaneNo = attrs.getIntReporting(SUMO_ATTR_NUMLANES, myCurrentID.c_str(), ok);
    }
    // try to get the priority
    if (attrs.hasAttribute(SUMO_ATTR_PRIORITY)) {
        myCurrentPriority = attrs.getIntReporting(SUMO_ATTR_PRIORITY, myCurrentID.c_str(), ok);
    }
    // try to get the width
    if (attrs.hasAttribute(SUMO_ATTR_WIDTH)) {
        myCurrentWidth = attrs.getSUMORealReporting(SUMO_ATTR_WIDTH, myCurrentID.c_str(), ok);
    }
    // try to get the width
    if (attrs.hasAttribute(SUMO_ATTR_ENDOFFSET)) {
        myCurrentOffset = attrs.getSUMORealReporting(SUMO_ATTR_ENDOFFSET, myCurrentID.c_str(), ok);
    }
    // try to get the street name
    myCurrentStreetName = attrs.getOptStringReporting(SUMO_ATTR_NAME, myCurrentID.c_str(), ok, myCurrentStreetName);

    // try to get the allowed/disallowed classes
    if (attrs.hasAttribute(SUMO_ATTR_ALLOW) || attrs.hasAttribute(SUMO_ATTR_DISALLOW)) {
        std::string allowS = attrs.hasAttribute(SUMO_ATTR_ALLOW) ? attrs.getStringSecure(SUMO_ATTR_ALLOW, "") : "";
        std::string disallowS = attrs.hasAttribute(SUMO_ATTR_DISALLOW) ? attrs.getStringSecure(SUMO_ATTR_DISALLOW, "") : "";
        // XXX matter of interpretation: should updated permissions replace or extend previously set permissions?
        myPermissions = parseVehicleClasses(allowS, disallowS);
    }
    // try to set the nodes
    if (!setNodes(attrs)) {
        // return if this failed
        return;
    }
    // try to get the shape
    myShape = tryGetShape(attrs);
    // try to get the spread type
    myLanesSpread = tryGetLaneSpread(attrs);
    // try to get the length
    myLength = attrs.getOptSUMORealReporting(SUMO_ATTR_LENGTH, myCurrentID.c_str(), ok, myLength);
    // insert the parsed edge into the edges map
    if (!ok) {
        return;
    }
    // check whether a previously defined edge shall be overwritten
    if (myCurrentEdge != 0) {
        myCurrentEdge->reinit(myFromNode, myToNode, myCurrentType, myCurrentSpeed,
                              myCurrentLaneNo, myCurrentPriority, myShape,
                              myCurrentWidth, myCurrentOffset, 
                              myCurrentStreetName, myLanesSpread,
                              OptionsCont::getOptions().getBool("plain.keep-edge-shape"));
    } else {
        // the edge must be allocated in dependence to whether a shape is given
        if (myShape.size() == 0) {
            myCurrentEdge = new NBEdge(myCurrentID, myFromNode, myToNode, myCurrentType, myCurrentSpeed,
                                       myCurrentLaneNo, myCurrentPriority, myCurrentWidth, myCurrentOffset,
                                       myCurrentStreetName, myLanesSpread);
        } else {
            myCurrentEdge = new NBEdge(myCurrentID, myFromNode, myToNode, myCurrentType, myCurrentSpeed,
                                       myCurrentLaneNo, myCurrentPriority, myCurrentWidth, myCurrentOffset,
                                       myShape, myCurrentStreetName, myLanesSpread,
                                       OptionsCont::getOptions().getBool("plain.keep-edge-shape"));
        }
    }
    myCurrentEdge->setLoadedLength(myLength);
    myCurrentEdge->setPermissions(myPermissions);
}


void
NIXMLEdgesHandler::addLane(const SUMOSAXAttributes& attrs) {
    if (myCurrentEdge == 0) {
        if (!OptionsCont::getOptions().isInStringVector("remove-edges.explicit", myCurrentID)) {
            WRITE_ERROR("Additional lane information could not been set - the edge with id '" + myCurrentID + "' is not known.");
        }
        return;
    }
    bool ok = true;
    int lane;
    if (attrs.hasAttribute(SUMO_ATTR_ID)) {
        lane = attrs.getIntReporting(SUMO_ATTR_ID, myCurrentID.c_str(), ok);
        if (!myHaveWarnedAboutDeprecatedLaneId) {
            myHaveWarnedAboutDeprecatedLaneId = true;
            WRITE_WARNING("'" + toString(SUMO_ATTR_ID) + "' is deprecated, please use '" + toString(SUMO_ATTR_INDEX) + "' instead.");
        }
    } else {
        lane = attrs.getIntReporting(SUMO_ATTR_INDEX, myCurrentID.c_str(), ok);
    }
    std::string allowed, disallowed, preferred;
    allowed    = attrs.getOptStringReporting(SUMO_ATTR_ALLOW, 0, ok, "");
    disallowed = attrs.getOptStringReporting(SUMO_ATTR_DISALLOW, 0, ok, "");
    preferred  = attrs.getOptStringReporting(SUMO_ATTR_PREFER, 0, ok, "");
    if (!ok) {
        return;
    }
    // check whether this lane exists
    if (lane >= (int) myCurrentEdge->getNumLanes()) {
        WRITE_ERROR("Lane index is larger than number of lanes (edge '" + myCurrentID + "').");
        return;
    }
    // set information about allowed / disallowed vehicle classes
    myCurrentEdge->setPermissions(parseVehicleClasses(allowed, disallowed), lane);
    myCurrentEdge->setPreferredVehicleClass(parseVehicleClasses(preferred), lane);
    // try to get the width
    if (attrs.hasAttribute(SUMO_ATTR_WIDTH)) {
        myCurrentEdge->setWidth(lane, attrs.getSUMORealReporting(SUMO_ATTR_WIDTH, myCurrentID.c_str(), ok));
    }
    // try to get the end-offset (lane shortened due to pedestrian crossing etc..)
    if (attrs.hasAttribute(SUMO_ATTR_ENDOFFSET)) {
        myCurrentEdge->setOffset(lane, attrs.getSUMORealReporting(SUMO_ATTR_ENDOFFSET, myCurrentID.c_str(), ok));
    }
    // try to get lane specific speed (should not occur for german networks)
    if (attrs.hasAttribute(SUMO_ATTR_SPEED)) {
        myCurrentEdge->setSpeed(lane, attrs.getSUMORealReporting(SUMO_ATTR_SPEED, myCurrentID.c_str(), ok));
    }
}


void NIXMLEdgesHandler::addSplit(const SUMOSAXAttributes& attrs) {
    if (myCurrentEdge == 0) {
        WRITE_WARNING("Ignoring 'split' because it cannot be assigned to an edge");
        return;
    }
    bool ok = true;
    Split e;
    e.pos = attrs.getSUMORealReporting(SUMO_ATTR_POSITION, 0, ok);
    if (ok) {
        if (fabs(e.pos) > myCurrentEdge->getGeometry().length()) {
            WRITE_ERROR("Edge '" + myCurrentID + "' has a split at invalid position " + toString(e.pos) + ".");
            return;
        }
        std::vector<Split>::iterator i = find_if(mySplits.begin(), mySplits.end(), split_by_pos_finder(e.pos));
        if (i != mySplits.end()) {
            WRITE_ERROR("Edge '" + myCurrentID + "' has already a split at position " + toString(e.pos) + ".");
            return;
        }
        e.nameid = (int)e.pos;
        if (myCurrentEdge == 0) {
            if (!OptionsCont::getOptions().isInStringVector("remove-edges.explicit", myCurrentID)) {
                WRITE_ERROR("Additional lane information could not been set - the edge with id '" + myCurrentID + "' is not known.");
            }
            return;
        }
        if (e.pos < 0) {
            e.pos += myCurrentEdge->getGeometry().length();
        }
        std::vector<std::string> lanes;
        SUMOSAXAttributes::parseStringVector(attrs.getOptStringReporting(SUMO_ATTR_LANES, 0, ok, ""), lanes);
        for (std::vector<std::string>::iterator i = lanes.begin(); i != lanes.end(); ++i) {
            try {
                int lane = TplConvert<char>::_2int((*i).c_str());
                e.lanes.push_back(lane);
            } catch (NumberFormatException&) {
                WRITE_ERROR("Error on parsing a split (edge '" + myCurrentID + "').");
            } catch (EmptyData&) {
                WRITE_ERROR("Error on parsing a split (edge '" + myCurrentID + "').");
            }
        }
        if (e.lanes.empty()) {
            for (size_t l = 0; l < myCurrentEdge->getNumLanes(); ++l) {
                e.lanes.push_back((int) l);
            }
        }
        mySplits.push_back(e);
    }
}


bool
NIXMLEdgesHandler::setNodes(const SUMOSAXAttributes& attrs) {
    // the names and the coordinates of the beginning and the end node
    // may be found, try
    bool ok = true;
    std::string begNodeID = myIsUpdate ? myCurrentEdge->getFromNode()->getID() : "";
    std::string endNodeID = myIsUpdate ? myCurrentEdge->getToNode()->getID() : "";
    std::string oldBegID = begNodeID;
    std::string oldEndID = endNodeID;
    if (attrs.hasAttribute(SUMO_ATTR_FROMNODE)) {
        begNodeID = attrs.getStringReporting(SUMO_ATTR_FROMNODE, 0, ok);
        if (!myHaveWarnedAboutDeprecatedFromTo) {
            WRITE_WARNING("'" + toString(SUMO_ATTR_FROMNODE) + "' is deprecated; please use '" + toString(SUMO_ATTR_FROM) + "'.");
            myHaveWarnedAboutDeprecatedFromTo = true;
        }
    }
    if (attrs.hasAttribute(SUMO_ATTR_TONODE)) {
        endNodeID = attrs.getStringReporting(SUMO_ATTR_TONODE, 0, ok);
        if (!myHaveWarnedAboutDeprecatedFromTo) {
            WRITE_WARNING("'" + toString(SUMO_ATTR_TONODE) + "' is deprecated; please use '" + toString(SUMO_ATTR_TO) + "'.");
            myHaveWarnedAboutDeprecatedFromTo = true;
        }
    }
    begNodeID = attrs.hasAttribute(SUMO_ATTR_FROM) ? attrs.getStringReporting(SUMO_ATTR_FROM, 0, ok) : begNodeID;
    endNodeID = attrs.hasAttribute(SUMO_ATTR_TO) ? attrs.getStringReporting(SUMO_ATTR_TO, 0, ok) : endNodeID;
    if (!ok) {
        return false;
    }
    // or their positions !!! deprecated
    SUMOReal begNodeXPos = tryGetPosition(attrs, SUMO_ATTR_XFROM, "XFrom");
    SUMOReal begNodeYPos = tryGetPosition(attrs, SUMO_ATTR_YFROM, "YFrom");
    SUMOReal endNodeXPos = tryGetPosition(attrs, SUMO_ATTR_XTO, "XTo");
    SUMOReal endNodeYPos = tryGetPosition(attrs, SUMO_ATTR_YTO, "YTo");
    if (begNodeXPos != SUMOXML_INVALID_POSITION && begNodeYPos != SUMOXML_INVALID_POSITION) {
        Position pos(begNodeXPos, begNodeYPos);
        NILoader::transformCoordinates(pos);
        begNodeXPos = pos.x();
        begNodeYPos = pos.y();
        if (!myHaveWarnedAboutDeprecatedFromTo) {
            WRITE_WARNING("'" + toString(SUMO_ATTR_XFROM) + "' and '" + toString(SUMO_ATTR_YFROM) + "' are deprecated; please define nodes separately.");
            myHaveWarnedAboutDeprecatedFromTo = true;
        }
    }
    if (endNodeXPos != SUMOXML_INVALID_POSITION && endNodeYPos != SUMOXML_INVALID_POSITION) {
        Position pos(endNodeXPos, endNodeYPos);
        NILoader::transformCoordinates(pos);
        endNodeXPos = pos.x();
        endNodeYPos = pos.y();
        if (!myHaveWarnedAboutDeprecatedFromTo) {
            WRITE_WARNING("'" + toString(SUMO_ATTR_XTO) + "' and '" + toString(SUMO_ATTR_YTO) + "' are deprecated; please define nodes separately.");
            myHaveWarnedAboutDeprecatedFromTo = true;
        }
    }
    // check the obtained values for nodes
    myFromNode = insertNodeChecking(Position(begNodeXPos, begNodeYPos), begNodeID, "from");
    myToNode = insertNodeChecking(Position(endNodeXPos, endNodeYPos), endNodeID, "to");
    if (myFromNode != 0 && myToNode != 0) {
        if (myIsUpdate && (myFromNode->getID() != oldBegID || myToNode->getID() != oldEndID)) {
            myShape = PositionVector();
        }
    }
    return myFromNode != 0 && myToNode != 0;
}


SUMOReal
NIXMLEdgesHandler::tryGetPosition(const SUMOSAXAttributes& attrs, SumoXMLAttr attrID,
                                  const std::string& attrName) {
    UNUSED_PARAMETER(attrName);
    bool ok = true;
    return attrs.getOptSUMORealReporting(attrID, myCurrentID.c_str(), ok, SUMOXML_INVALID_POSITION);
}


NBNode*
NIXMLEdgesHandler::insertNodeChecking(const Position& pos,
                                      const std::string& name, const std::string& dir) {
    NBNode* ret = 0;
    if (name == "" && (pos.x() == SUMOXML_INVALID_POSITION || pos.y() == SUMOXML_INVALID_POSITION)) {
        WRITE_ERROR("Neither the name nor the position of the " + dir + "-node is given for edge '" + myCurrentID + "'.");
        return ret;
    }
    if (name != "") {
        if (pos.x() != SUMOXML_INVALID_POSITION && pos.y() != SUMOXML_INVALID_POSITION) {
            // the node is named and it has a position given
            if (!myNodeCont.insert(name, pos)) {
                WRITE_ERROR("Position of " + dir + "-node '" + name + "' mismatches previous positions.");
                return 0;
            }
        }
        // the node is given by its name
        ret = myNodeCont.retrieve(name);
        if (ret == 0) {
            WRITE_ERROR("Edge's '" + myCurrentID + "' " + dir + "-node '" + name + "' is not known.");
        }
    } else {
        ret = myNodeCont.retrieve(pos);
        if (ret == 0) {
            ret = new NBNode(myNodeCont.getFreeID(), pos);
            if (!myNodeCont.insert(ret)) {
                WRITE_ERROR("Could not insert " + dir + "-node at position " + toString(pos) + ".");
                delete ret;
                return 0;
            }
        }
    }
    return ret;
}


PositionVector
NIXMLEdgesHandler::tryGetShape(const SUMOSAXAttributes& attrs) {
    if (!attrs.hasAttribute(SUMO_ATTR_SHAPE)) {
        return myShape;
    }
    // try to build shape
    bool ok = true;
    std::string shpdef = attrs.getOptStringReporting(SUMO_ATTR_SHAPE, 0, ok, "");
    if (shpdef == "") {
        return PositionVector();
    }
    PositionVector shape = GeomConvHelper::parseShapeReporting(shpdef, attrs.getObjectType(), 0, ok, true);
    if (!NILoader::transformCoordinates(shape)) {
        WRITE_ERROR("Unable to project coordinates for edge '" + myCurrentID + "'.");
    }
    return shape;
}


LaneSpreadFunction
NIXMLEdgesHandler::tryGetLaneSpread(const SUMOSAXAttributes& attrs) {
    bool ok = true;
    LaneSpreadFunction result = myLanesSpread;
    std::string lsfS = toString(result);
    if (attrs.hasAttribute(SUMO_ATTR_SPREADFUNC__DEPRECATED)) {
        lsfS = attrs.getStringReporting(SUMO_ATTR_SPREADFUNC__DEPRECATED, myCurrentID.c_str(), ok);
        if (!myHaveWarnedAboutDeprecatedSpreadType) {
            WRITE_WARNING("'" + toString(SUMO_ATTR_SPREADFUNC__DEPRECATED) + " is deprecated; please use '" + toString(SUMO_ATTR_SPREADTYPE) + "'.");
            myHaveWarnedAboutDeprecatedSpreadType = true;
        }
    } else {
        lsfS = attrs.getOptStringReporting(SUMO_ATTR_SPREADTYPE, myCurrentID.c_str(), ok, lsfS);
    }
    if (SUMOXMLDefinitions::LaneSpreadFunctions.hasString(lsfS)) {
        result = SUMOXMLDefinitions::LaneSpreadFunctions.get(lsfS);
    } else {
        WRITE_WARNING("Ignoring unknown spreadType '" + lsfS + "' for edge '" + myCurrentID + "'.");
    }
    return result;
}


void
NIXMLEdgesHandler::deleteEdge(const SUMOSAXAttributes& attrs) {
    bool ok = true;
    myCurrentID = attrs.getStringReporting(SUMO_ATTR_ID, 0, ok);
    if (!ok) {
        return;
    }
    NBEdge* edge = myEdgeCont.retrieve(myCurrentID);
    if (edge == 0) {
        WRITE_WARNING("Ignoring tag '" + toString(SUMO_TAG_DELETE) + "' for unknown edge '" +
                      myCurrentID + "'");
        return;
    }
    myEdgeCont.extract(myDistrictCont, edge, true);
}


void
NIXMLEdgesHandler::myEndElement(int element) {
    if (element == SUMO_TAG_EDGE && myCurrentEdge != 0) {
        if (!myIsUpdate) {
            try {
                if (!myEdgeCont.insert(myCurrentEdge)) {
                    WRITE_ERROR("Duplicate edge occured. ID='" + myCurrentID + "'");
                    delete myCurrentEdge;
                }
            } catch (InvalidArgument& e) {
                WRITE_ERROR(e.what());
                throw;
            } catch (...) {
                WRITE_ERROR("An important information is missing in edge '" + myCurrentID + "'.");
            }
        }
        if (mySplits.size() != 0) {
            std::vector<Split>::iterator i;
            NBEdge* e = myCurrentEdge;
            SUMOReal length = e->getLength();
            sort(mySplits.begin(), mySplits.end(), split_sorter());
            unsigned int noLanesMax = e->getNumLanes();
            // compute the node positions and sort the lanes
            for (i = mySplits.begin(); i != mySplits.end(); ++i) {
                (*i).gpos = e->getGeometry().positionAtLengthPosition((*i).pos);
                sort((*i).lanes.begin(), (*i).lanes.end());
                noLanesMax = MAX2(noLanesMax, (unsigned int)(*i).lanes.size());
            }
            // split the edge
            std::vector<int> currLanes;
            for (unsigned int l = 0; l < e->getNumLanes(); ++l) {
                currLanes.push_back(l);
            }
            std::string edgeid = e->getID();
            SUMOReal seen = 0;
            for (i = mySplits.begin(); i != mySplits.end(); ++i) {
                const Split& exp = *i;
                assert(exp.lanes.size() != 0);
                if (exp.pos > 0 && e->getGeometry().length() + seen > exp.pos && exp.pos > seen) {
                    std::string nid = edgeid + "." +  toString(exp.nameid);
                    NBNode* rn = new NBNode(nid, exp.gpos);
                    if (myNodeCont.insert(rn)) {
                        //  split the edge
                        std::string nid = myCurrentID + "." +  toString(exp.nameid);
                        std::string pid = e->getID();
                        myEdgeCont.splitAt(myDistrictCont, e, exp.pos - seen, rn,
                                           pid, nid, e->getNumLanes(), (unsigned int) exp.lanes.size());
                        seen = exp.pos;
                        std::vector<int> newLanes = exp.lanes;
                        NBEdge* pe = myEdgeCont.retrieve(pid);
                        NBEdge* ne = myEdgeCont.retrieve(nid);
                        // reconnect lanes
                        pe->invalidateConnections(true);
                        //  new on right
                        unsigned int rightMostP = currLanes[0];
                        unsigned int rightMostN = newLanes[0];
                        for (int l = 0; l < (int) rightMostP - (int) rightMostN; ++l) {
                            pe->addLane2LaneConnection(0, ne, l, NBEdge::L2L_VALIDATED, true);
                        }
                        //  new on left
                        unsigned int leftMostP = currLanes.back();
                        unsigned int leftMostN = newLanes.back();
                        for (int l = 0; l < (int) leftMostN - (int) leftMostP; ++l) {
                            pe->addLane2LaneConnection(pe->getNumLanes() - 1, ne, leftMostN - l - rightMostN, NBEdge::L2L_VALIDATED, true);
                        }
                        //  all other connected
                        for (unsigned int l = 0; l < noLanesMax; ++l) {
                            if (find(currLanes.begin(), currLanes.end(), l) == currLanes.end()) {
                                continue;
                            }
                            if (find(newLanes.begin(), newLanes.end(), l) == newLanes.end()) {
                                continue;
                            }
                            pe->addLane2LaneConnection(l - rightMostP, ne, l - rightMostN, NBEdge::L2L_VALIDATED, true);
                        }
                        // move to next
                        e = ne;
                        currLanes = newLanes;
                    } else {
                        WRITE_WARNING("Error on parsing a split (edge '" + myCurrentID + "').");
                    }
                }  else if (exp.pos == 0) {
                    if (e->getNumLanes() < exp.lanes.size()) {
                        e->incLaneNo((int) exp.lanes.size() - e->getNumLanes());
                    } else {
                        e->decLaneNo(e->getNumLanes() - (int) exp.lanes.size());
                    }
                    currLanes = exp.lanes;
                } else {
                    WRITE_WARNING("Split at '" + toString(exp.pos) + "' lies beyond the edge's length (edge '" + myCurrentID + "').");
                }
            }
            // patch lane offsets
            e = myEdgeCont.retrieve(edgeid);
            i = mySplits.begin();
            if ((*i).pos != 0) {
                e = e->getToNode()->getOutgoingEdges()[0];
            }
            for (; i != mySplits.end(); ++i) {
                unsigned int maxLeft = (*i).lanes.back();
                if (maxLeft < noLanesMax) {
                    PositionVector g = e->getGeometry();
                    g.move2side(SUMO_const_laneWidthAndOffset * (noLanesMax - 1 - maxLeft));
                    e->setGeometry(g);
                }
                if (e->getToNode()->getOutgoingEdges().size() != 0) {
                    e = e->getToNode()->getOutgoingEdges()[0];
                }
            }
        }
    }
}

/****************************************************************************/

