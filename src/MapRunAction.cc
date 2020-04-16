

#include "G4Timer.hh"
#include "MapRunAction.hh"
#include "G4Run.hh"


MapRunAction::MapRunAction(size_t nrOfVolumeIndices)
	: G4UserRunAction(), hitCount(nrOfVolumeIndices)
{

}

MapRunAction::~MapRunAction(){}

void MapRunAction::BeginOfRunAction(const G4Run*){
	for(size_t i = 0; i < hitCount.size(); i++){
		hitCount[i] = 0;	//reset counter
	}
}

void MapRunAction::EndOfRunAction(const G4Run*){
	for(size_t i = 0; i < hitCount.size(); i++){
		G4cout << "Vol "<<i<<" --> "<<hitCount[i] << " counts."<<std::endl;
	}
}

void MapRunAction::increment(G4int volID){
	//can be removed for speedup when we are confident, that no shit is going on
	if(volID < 0 || volID >= hitCount.size()){
		G4cout << "ERROR: volID out of bounds: "<<volID<<G4endl;
		throw 999;
	}
	hitCount[volID]++;
}
