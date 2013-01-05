/**
 * @file DartLoader.cpp
 */

#include "DartLoader.h"
#include <map>
#include "../urdf_parser/urdf_parser.h"
#include <iostream>
#include <fstream>
#include "dynamics/BodyNodeDynamics.h"
#include "dynamics/SkeletonDynamics.h"
#include "robotics/World.h"
#include "robotics/Robot.h"
#include "robotics/Object.h"

/**
 * @function DartLoader
 * @brief Constructor
 */
DartLoader::DartLoader() {
}

/**
 * @function ~DartLoader
 * @brief Destructor
 */
DartLoader::~DartLoader() {

}

/**
 * @function parseSkeleton
 */
dynamics::SkeletonDynamics* DartLoader::parseSkeleton( std::string _urdfFile ) {

  std::string xml_string;
  xml_string = readXmlToString( _urdfFile );

  boost::shared_ptr<urdf::ModelInterface> skeletonModel = urdf::parseURDF( xml_string );
  
  return modelInterfaceToSkeleton( skeletonModel );

}

/**
 * @function parseRobot
 */
robotics::Robot* DartLoader::parseRobot( std::string _urdfFile ) {

  std::string xml_string;
  xml_string = readXmlToString( _urdfFile );
  boost::shared_ptr<urdf::ModelInterface> robotModel = urdf::parseURDF( xml_string );
  return modelInterfaceToRobot( robotModel );
}

/**
 * @function parseObject
 */
robotics::Object* DartLoader::parseObject( std::string _urdfFile ) {
  
  std::string xml_string;
  xml_string = readXmlToString( _urdfFile );
  boost::shared_ptr<urdf::ModelInterface> objectModel = urdf::parseURDF( xml_string );
  return modelInterfaceToObject( objectModel );

}

/**
 * @function parseWorld
 */
robotics::World* DartLoader::parseWorld( std::string _urdfFile ) {

  mWorldPath = _urdfFile;
 
  // Change path to a Unix-style path if given a Windows one
  // Windows can handle Unix-style paths (apparently)
  std::replace( mWorldPath.begin(), mWorldPath.end(), '\\' , '/' );
  mPath = mWorldPath.substr( 0, mWorldPath.rfind("/") + 1 );

  // std::cout<< " mPath :" << mPath << std::endl;

  robotics::World* world = new robotics::World();
	robotics::Robot* robot;
  robotics::Object* object;

  std::string xml_string;
  xml_string = readXmlToString( _urdfFile );

  boost::shared_ptr<urdf::World> worldInterface =  urdf::parseWorldURDF(xml_string, mPath );

  double roll, pitch, yaw;
  for( unsigned int i = 0; i < worldInterface->objectModels.size(); ++i ) {
    
    object = modelInterfaceToObject(  worldInterface->objectModels[i].model );
    // Initialize position and RPY 
    worldInterface->objectModels[i].origin.rotation.getRPY( roll, pitch, yaw );
    object->setRotationRPY( roll, pitch, yaw );
    
    object->setPositionX( worldInterface->objectModels[i].origin.position.x ); 
    object->setPositionY( worldInterface->objectModels[i].origin.position.y ); 
    object->setPositionZ( worldInterface->objectModels[i].origin.position.z );
    object->update();
    world->addObject( object );
  }
  
  for( unsigned int i = 0; i < worldInterface->robotModels.size(); ++i )  {
    
    robot = modelInterfaceToRobot(  worldInterface->robotModels[i].model );
    // Initialize position and RPY 
    worldInterface->robotModels[i].origin.rotation.getRPY( roll, pitch, yaw );
    robot->setRotationRPY( roll, pitch, yaw );
    
    robot->setPositionX( worldInterface->robotModels[i].origin.position.x ); 
    robot->setPositionY( worldInterface->robotModels[i].origin.position.y ); 
    robot->setPositionZ( worldInterface->robotModels[i].origin.position.z );
    robot->update();
    world->addRobot( robot );
  }
  
  world->rebuildCollision();
  return world;
}

/**
 * @function modelInterfaceToSkeleton
 * @brief Read the ModelInterface and spits out a SkeletonDynamics object
 */
dynamics::SkeletonDynamics* DartLoader::modelInterfaceToSkeleton( boost::shared_ptr<urdf::ModelInterface> _model ) {
  
  dynamics::SkeletonDynamics* mSkeleton = new dynamics::SkeletonDynamics();
  dynamics::BodyNodeDynamics *node, *rootNode;
  kinematics::Joint *joint, *rootJoint;
  
  // BodyNode
  mNodes.resize(0);  
  for( std::map<std::string, boost::shared_ptr<urdf::Link> >::const_iterator lk = _model->links_.begin(); 
       lk != _model->links_.end(); 
       lk++ ) {
    node = createDartNode( (*lk).second, mSkeleton );
    mNodes.push_back( node );
  }
  if(debug) printf ("** Created %d body nodes \n", mNodes.size() );
  
  // Joint
  mJoints.resize(0);
  
  for( std::map<std::string, boost::shared_ptr<urdf::Joint> >::const_iterator jt = _model->joints_.begin(); 
       jt != _model->joints_.end(); 
       jt++ ) {  
    joint = createDartJoint( (*jt).second, mSkeleton );
    mJoints.push_back( joint );
    
  }
  
  //-- root joint
  rootNode = getNode( _model->getRoot()->name );
  rootJoint = createDartRootJoint( _model->getRoot(), mSkeleton, false );
  mJoints.push_back( rootJoint );

  if(debug) printf ("** Created %d joints \n", mJoints.size() );
  
  //-- Save DART structure
  
  // Push parents first
  std::list<dynamics::BodyNodeDynamics*> nodeStack;
  dynamics::BodyNodeDynamics* u;
  nodeStack.push_back( rootNode );
  
  int numIter = 0;
  while( !nodeStack.empty() && numIter < mNodes.size() ) {

    // Get front element on stack and add it
    u = nodeStack.front();
    mSkeleton->addNode(u);

    // Pop it out
    nodeStack.pop_front();
    
    // Add its kids
    for( int idx = 0; idx < u->getNumChildJoints(); ++idx ) {
      nodeStack.push_back( (dynamics::BodyNodeDynamics*)( u->getChildNode(idx) ) );
    }
    numIter++;
  }
  if(debug) printf ("--> Pushed %d nodes in tree-like order \n", numIter );
  
  // Init skeleton
  mSkeleton->initSkel();
  
  return mSkeleton;
}

/**
 * @function modelInterfaceToRobot
 */
robotics::Robot* DartLoader::modelInterfaceToRobot( boost::shared_ptr<urdf::ModelInterface> _model ) {

  robotics::Robot* mRobot;
  dynamics::BodyNodeDynamics *node, *rootNode;
  kinematics::Joint *joint, *rootJoint;

	mRobot = new robotics::Robot();
	// Add default root node (*)
  mRobot->addDefaultRootNode();

  // name
  mRobot->setName( _model->getName() );

  // BodyNode
  mNodes.resize(0);  
  for( std::map<std::string, boost::shared_ptr<urdf::Link> >::const_iterator lk = _model->links_.begin(); 
       lk != _model->links_.end(); 
       lk++ ) {
    node = createDartNode( (*lk).second, mRobot );
    mNodes.push_back( node );
  }
  if(debug) printf ("** Created %d body nodes \n", mNodes.size() );

  // Joint
  mJoints.resize(0);

  //-- root joint
  rootNode = getNode( _model->getRoot()->name );
  if(debug) printf ("[DartLoader] Root Node: %s \n", rootNode->getName() );
	// Set true argument since default root node was set (*)
  rootJoint = createDartRootJoint( _model->getRoot(), mRobot, true );
  mJoints.push_back( rootJoint );
  
  //-- Save DART structure
     // Push parents first
    std::list<dynamics::BodyNodeDynamics*> nodeStack;
    dynamics::BodyNodeDynamics* u;
    nodeStack.push_back( rootNode );

    int numIter = 0;
    while( !nodeStack.empty() && numIter < mNodes.size() ) {
      // Get front element on stack and update it
      u = nodeStack.front();
			mRobot->addNode(u);

  		for( std::map<std::string, boost::shared_ptr<urdf::Joint> >::const_iterator jt = _model->joints_.begin(); 
       jt != _model->joints_.end(); 
       jt++ ) {  
				if( ( (*jt).second )->parent_link_name == u->getName() ) {
	    		joint = createDartJoint( (*jt).second, mRobot );
  	  		mJoints.push_back( joint );
				}
  		}

      // Pop it out
      nodeStack.pop_front();

      // Add its kids
      for( int idx = 0; idx < u->getNumChildJoints(); ++idx ) {
	nodeStack.push_back( (dynamics::BodyNodeDynamics*)( u->getChildNode(idx) ) );
      }
      numIter++;
    } // end while

	  if(debug) printf ("** Created %d joints \n", mJoints.size() );
		if(debug) printf ("Pushed %d nodes in order \n", numIter );
  
  // Init robot (skeleton)
  mRobot->initSkel();
  mRobot->update();
  return mRobot;
}

/**
 * @function modelInterfaceToObject
 */
robotics::Object* DartLoader::modelInterfaceToObject( boost::shared_ptr<urdf::ModelInterface> _model ) {
  
  robotics::Object* mObject;
  dynamics::BodyNodeDynamics *node, *rootNode;
  kinematics::Joint *joint, *rootJoint;

  mObject = new robotics::Object();
  mObject->addDefaultRootNode();
  
  // name
  mObject->setName( _model->getName() );
  
  // BodyNode
  mNodes.resize(0);  
  for( std::map<std::string, boost::shared_ptr<urdf::Link> >::const_iterator lk = _model->links_.begin(); 
       lk != _model->links_.end(); 
       lk++ ) {
    node = createDartNode( (*lk).second, mObject );
    mNodes.push_back( node );
  }
  if(debug) printf ("** Created %d body nodes \n", mNodes.size() );
  
  // Joint
  mJoints.resize(0);
  
  for( std::map<std::string, boost::shared_ptr<urdf::Joint> >::const_iterator jt = _model->joints_.begin(); 
       jt != _model->joints_.end(); 
       jt++ ) {  
    joint = createDartJoint( (*jt).second, mObject );
    mJoints.push_back( joint );

  }
  
  //-- root joint
  rootNode = getNode( _model->getRoot()->name );
  rootJoint = createDartRootJoint( _model->getRoot(), mObject, true );
  mJoints.push_back( rootJoint );
  
  if(debug) printf ("** Created %d joints \n", mJoints.size() );
  
  //-- Save DART structure
  
  // 1. Root node
  mObject->addNode( rootNode );
  
  // 2. The rest
  for( unsigned int i = 0; i < mNodes.size(); ++i ) {
    if( mNodes[i] != rootNode ) {
      mObject->addNode( mNodes[i] );
    }
  }
  
  // Init robot (skeleton)
  mObject->initSkel();
  mObject->update();
  
  return mObject;
}

/**
 * @function getNode
 */
dynamics::BodyNodeDynamics* DartLoader::getNode( std::string _nodeName ) {

  for( unsigned int i = 0; i < mNodes.size(); ++i ) {
    std::string node( mNodes[i]->getName() );
    if( node ==  _nodeName ) {
      return mNodes[i];
    }
  }
  if(debug) printf ("[getNode] ERROR: Returning  NULL for  %s \n", _nodeName.c_str() );
  return NULL;
}

/**
 * @function readXml
 */
std::string  DartLoader::readXmlToString( std::string _xmlFile ) {
  
  std::string xml_string;
  
  std::fstream xml_file( _xmlFile.c_str(), std::fstream::in );
  
  // Read xml
  while( xml_file.good() ) {
    std::string line;
    std::getline( xml_file, line );
    xml_string += (line + "\n");
  }
  xml_file.close();
  
  return xml_string;
}
