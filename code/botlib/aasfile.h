/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#pragma once


//NOTE:	int =	default signed
//				default long

static constexpr int AASID = ('S' << 24) + ('A' << 16) + ('A' << 8) + 'E';
static constexpr int AASVERSION_OLD = 4;
static constexpr int AASVERSION = 5;

//presence types
static constexpr int PRESENCE_NONE = 1;
static constexpr int PRESENCE_NORMAL = 2;
static constexpr int PRESENCE_CROUCH = 4;

//travel types
static constexpr int MAX_TRAVELTYPES = 32;
static constexpr int TRAVEL_INVALID = 1;		//temporary not possible
static constexpr int TRAVEL_WALK = 2;			//walking
static constexpr int TRAVEL_CROUCH = 3;			//crouching
static constexpr int TRAVEL_BARRIERJUMP = 4;	//jumping onto a barrier
static constexpr int TRAVEL_JUMP = 5;			//jumping
static constexpr int TRAVEL_LADDER = 6;			//climbing a ladder
static constexpr int TRAVEL_WALKOFFLEDGE = 7;	//walking of a ledge
static constexpr int TRAVEL_SWIM = 8;			//swimming
static constexpr int TRAVEL_WATERJUMP = 9;		//jump out of the water
static constexpr int TRAVEL_TELEPORT = 10;		//teleportation
static constexpr int TRAVEL_ELEVATOR = 11;		//travel by elevator
static constexpr int TRAVEL_ROCKETJUMP = 12;	//rocket jumping required for travel
static constexpr int TRAVEL_BFGJUMP = 13;		//bfg jumping required for travel
static constexpr int TRAVEL_GRAPPLEHOOK = 14;	//grappling hook required for travel
static constexpr int TRAVEL_DOUBLEJUMP = 15;	//double jump
static constexpr int TRAVEL_RAMPJUMP = 16;		//ramp jump
static constexpr int TRAVEL_STRAFEJUMP = 17;	//strafe jump
static constexpr int TRAVEL_JUMPPAD = 18;		//jump pad
static constexpr int TRAVEL_FUNCBOB = 19;		//func bob

//additional travel flags
static constexpr int TRAVELTYPE_MASK = 0xFFFFFF;
static constexpr int TRAVELFLAG_NOTTEAM1 = 1 << 24;
static constexpr int TRAVELFLAG_NOTTEAM2 = 2 << 24;

//face flags
static constexpr int FACE_SOLID = 1;			//just solid at the other side
static constexpr int FACE_LADDER = 2;			//ladder
static constexpr int FACE_GROUND = 4;			//standing on ground when in this face
static constexpr int FACE_GAP = 8;				//gap in the ground
static constexpr int FACE_LIQUID = 16;			//face separating two areas with liquid
static constexpr int FACE_LIQUIDSURFACE = 32;	//face separating liquid and air
static constexpr int FACE_BRIDGE = 64;			//can walk over this face if bridge is closed

//area contents
static constexpr int AREACONTENTS_WATER = 1;
static constexpr int AREACONTENTS_LAVA = 2;
static constexpr int AREACONTENTS_SLIME = 4;
static constexpr int AREACONTENTS_CLUSTERPORTAL = 8;
static constexpr int AREACONTENTS_TELEPORTAL = 16;
static constexpr int AREACONTENTS_ROUTEPORTAL = 32;
static constexpr int AREACONTENTS_TELEPORTER = 64;
static constexpr int AREACONTENTS_JUMPPAD = 128;
static constexpr int AREACONTENTS_DONOTENTER = 256;
static constexpr int AREACONTENTS_VIEWPORTAL = 512;
static constexpr int AREACONTENTS_MOVER = 1024;
static constexpr int AREACONTENTS_NOTTEAM1 = 2048;
static constexpr int AREACONTENTS_NOTTEAM2 = 4096;
//number of model of the mover inside this area
static constexpr int AREACONTENTS_MODELNUMSHIFT = 24;
static constexpr int AREACONTENTS_MAXMODELNUM = 0xFF;
static constexpr int AREACONTENTS_MODELNUM = AREACONTENTS_MAXMODELNUM << AREACONTENTS_MODELNUMSHIFT;

//area flags
static constexpr int AREA_GROUNDED = 1;		//bot can stand on the ground
static constexpr int AREA_LADDER = 2;		//area contains one or more ladder faces
static constexpr int AREA_LIQUID = 4;		//area contains a liquid
static constexpr int AREA_DISABLED = 8;		//area is disabled for routing when set
static constexpr int AREA_BRIDGE = 16;		//area ontop of a bridge

//aas file header lumps
static constexpr int AAS_LUMPS = 14;
static constexpr int AASLUMP_BBOXES = 0;
static constexpr int AASLUMP_VERTEXES = 1;
static constexpr int AASLUMP_PLANES = 2;
static constexpr int AASLUMP_EDGES = 3;
static constexpr int AASLUMP_EDGEINDEX = 4;
static constexpr int AASLUMP_FACES = 5;
static constexpr int AASLUMP_FACEINDEX = 6;
static constexpr int AASLUMP_AREAS = 7;
static constexpr int AASLUMP_AREASETTINGS = 8;
static constexpr int AASLUMP_REACHABILITY = 9;
static constexpr int AASLUMP_NODES = 10;
static constexpr int AASLUMP_PORTALS = 11;
static constexpr int AASLUMP_PORTALINDEX = 12;
static constexpr int AASLUMP_CLUSTERS = 13;

//========== bounding box =========

//bounding box
struct aas_bbox_s
{
	int presencetype;
	int flags;
	vec3_t mins, maxs;
};
using aas_bbox_t = aas_bbox_s;

//============ settings ===========

//reachability to another area
struct aas_reachability_s
{
	int areanum;						//number of the reachable area
	int facenum;						//number of the face towards the other area
	int edgenum;						//number of the edge towards the other area
	vec3_t start;						//start point of inter area movement
	vec3_t end;							//end point of inter area movement
	int traveltype;					//type of travel required to get to the area
	unsigned short int traveltime;//travel time of the inter area movement
};
using aas_reachability_t = aas_reachability_s;

//area settings
struct aas_areasettings_s
{
	//could also add all kind of statistic fields
	int contents;						//contents of the area
	int areaflags;						//several area flags
	int presencetype;					//how a bot can be present in this area
	int cluster;						//cluster the area belongs to, if negative it's a portal
	int clusterareanum;				//number of the area in the cluster
	int numreachableareas;			//number of reachable areas from this one
	int firstreachablearea;			//first reachable area in the reachable area index
};
using aas_areasettings_t = aas_areasettings_s;

//cluster portal
struct aas_portal_s
{
	int areanum;						//area that is the actual portal
	int frontcluster;					//cluster at front of portal
	int backcluster;					//cluster at back of portal
	int clusterareanum[2];			//number of the area in the front and back cluster
};
using aas_portal_t = aas_portal_s;

//cluster portal index
using aas_portalindex_t = int;

//cluster
struct aas_cluster_s
{
	int numareas;						//number of areas in the cluster
	int numreachabilityareas;			//number of areas with reachabilities
	int numportals;						//number of cluster portals
	int firstportal;					//first cluster portal in the index
};
using aas_cluster_t = aas_cluster_s;

//============ 3d definition ============

using aas_vertex_t = vec3_t;

//just a plane in the third dimension
struct aas_plane_s
{
	vec3_t normal;						//normal vector of the plane
	float dist;							//distance of the plane (normal vector * distance = point in plane)
	int type;
};
using aas_plane_t = aas_plane_s;

//edge
struct aas_edge_s
{
	int v[2];							//numbers of the vertexes of this edge
};
using aas_edge_t = aas_edge_s;

//edge index, negative if vertexes are reversed
using aas_edgeindex_t = int;

//a face bounds an area, often it will also separate two areas
struct aas_face_s
{
	int planenum;						//number of the plane this face is in
	int faceflags;						//face flags (no use to create face settings for just this field)
	int numedges;						//number of edges in the boundary of the face
	int firstedge;						//first edge in the edge index
	int frontarea;						//area at the front of this face
	int backarea;						//area at the back of this face
};
using aas_face_t = aas_face_s;

//face index, stores a negative index if backside of face
using aas_faceindex_t = int;

//area with a boundary of faces
struct aas_area_s
{
	int areanum;						//number of this area
	//3d definition
	int numfaces;						//number of faces used for the boundary of the area
	int firstface;						//first face in the face index used for the boundary of the area
	vec3_t mins;						//mins of the area
	vec3_t maxs;						//maxs of the area
	vec3_t center;						//'center' of the area
};
using aas_area_t = aas_area_s;

//nodes of the bsp tree
struct aas_node_s
{
	int planenum;
	int children[2];					//child nodes of this node, or areas as leaves when negative
										//when a child is zero it's a solid leaf
};
using aas_node_t = aas_node_s;

//=========== aas file ===============

//header lump
struct aas_lump_t
{
	int fileofs;
	int filelen;
};

//aas file header
struct aas_header_s
{
	int ident;
	int version;
	int bspchecksum;
	//data entries
	aas_lump_t lumps[AAS_LUMPS];
};
using aas_header_t = aas_header_s;


//====== additional information ======
/*

-	when a node child is a solid leaf the node child number is zero
-	two adjacent areas (sharing a plane at opposite sides) share a face
	this face is a portal between the areas
-	when an area uses a face from the faceindex with a positive index
	then the face plane normal points into the area
-	the face edges are stored counter clockwise using the edgeindex
-	two adjacent convex areas (sharing a face) only share One face
	this is a simple result of the areas being convex
-	the areas can't have a mixture of ground and gap faces
	other mixtures of faces in one area are allowed
-	areas with the AREACONTENTS_CLUSTERPORTAL in the settings have
	the cluster number set to the negative portal number
-	edge zero is a dummy
-	face zero is a dummy
-	area zero is a dummy
-	node zero is a dummy
*/
