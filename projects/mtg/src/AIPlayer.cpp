#include "PrecompiledHeader.h"

#include "AIPlayer.h"
#include "CardDescriptor.h"
#include "AIStats.h"
#include "AllAbilities.h"
#include "ExtraCost.h"
#include "GuiCombat.h"
#include "GameStateDuel.h"

const char * const MTG_LAND_TEXTS[] = {"artifact","forest","island","mountain","swamp","plains","other lands"};

int AIAction::currentId = 0;

int AIAction::Act(){
  GameObserver * g = GameObserver::GetInstance();
  if (player){
    g->cardClick(NULL, player);
    return 1;
  }
  if (ability){
    g->mLayers->actionLayer()->reactToClick(ability,click);
    if (target) g->cardClick(target);
    return 1;
  }else if (click){ //Shouldn't be used, really...
    g->cardClick(click,click);
    if (target) g->cardClick(target);
    return 1;
  }
  return 0;
}

AIPlayer::AIPlayer(MTGDeck * deck, string file, string fileSmall) : Player(deck, file, fileSmall) {
  nextCardToPlay = NULL;
  stats = NULL;
  agressivity = 50;
  forceBestAbilityUse = false;
  playMode = Player::MODE_AI;
}

AIPlayer::~AIPlayer(){
  if (stats){
    stats->save();
    SAFE_DELETE(stats);
  }
  while(!clickstream.empty()){
    AIAction * action = clickstream.front();
    SAFE_DELETE(action);
    clickstream.pop();
  }
}
MTGCardInstance * AIPlayer::chooseCard(TargetChooser * tc, MTGCardInstance * source, int random){
  for (int i = 0; i < game->hand->nb_cards; i++){
    MTGCardInstance * card = game->hand->cards[i];
    if (!tc->alreadyHasTarget(card) && tc->canTarget(card)){
      return card;
    }
  }
  return NULL;
}

int AIPlayer::Act(float dt){
  GameObserver * gameObs = GameObserver::GetInstance();
  if (gameObs->currentPlayer == this)
    gameObs->userRequestNextGamePhase();
  return 1;
}


void AIPlayer::tapLandsForMana(ManaCost * cost,MTGCardInstance * target){
  if (!cost) return;
  DebugTrace(" AI tapping land for mana");

  ManaCost * pMana = getPotentialMana(target);
  ManaCost * diff = pMana->Diff(cost);
  delete(pMana);
  GameObserver * g = GameObserver::GetInstance();

  map<MTGCardInstance *,bool>used;
  for (int i = 1; i < g->mLayers->actionLayer()->mCount; i++){ //0 is not a mtgability...hackish
    //Make sure we can use the ability
    MTGAbility * a = ((MTGAbility *)g->mLayers->actionLayer()->mObjects[i]);
    AManaProducer * amp = dynamic_cast<AManaProducer*>(a);
    if (amp && canHandleCost(amp)){
      MTGCardInstance * card = amp->source;
      if (card == target) used[card] = true; //http://code.google.com/p/wagic/issues/detail?id=76
      if (!used[card] && amp->isReactingToClick(card) && amp->output->getConvertedCost()==1){
        used[card] = true;
        int doTap = 1;
        for (int i=Constants::MTG_NB_COLORS-1; i>= 0; i--){
          if (diff->getCost(i) &&  amp->output->getCost(i) ){
	          diff->remove(i,1);
            doTap = 0;
            break;
          }
        }
        if (doTap){
          AIAction * action = NEW AIAction(amp,card);
          clickstream.push(action);
        }
      }
    }
  }
  delete(diff);

}

ManaCost * AIPlayer::getPotentialMana(MTGCardInstance * target){
  ManaCost * result = NEW ManaCost();
  GameObserver * g = GameObserver::GetInstance();
  map<MTGCardInstance *,bool>used;
  for (int i = 1; i < g->mLayers->actionLayer()->mCount; i++){ //0 is not a mtgability...hackish
    //Make sure we can use the ability
    MTGAbility * a = ((MTGAbility *)g->mLayers->actionLayer()->mObjects[i]);
    AManaProducer * amp = dynamic_cast<AManaProducer*>(a);
    if (amp && canHandleCost(amp)){
      MTGCardInstance * card = amp->source;
      if (card == target) used[card] = true; //http://code.google.com/p/wagic/issues/detail?id=76
      if (!used[card] && amp->isReactingToClick(card) && amp->output->getConvertedCost()==1){
        result->add(amp->output);
        used[card] = true;
      }
    }
	}

  return result;
}


int AIPlayer::getEfficiency(AIAction * action){
  return action->getEfficiency();
}

int AIPlayer::canHandleCost(MTGAbility * ability){
    //Can't handle sacrifice costs that require a target yet :(
  if (ability->cost){
    ExtraCosts * ec = ability->cost->extraCosts;
    if (ec){
      for (size_t i = 0; i < ec->costs.size(); i++){
        if (ec->costs[i]->tc) return 0;
      }
    }
  }
  return 1;
}



int AIAction::getEfficiency(){
  //TODO add multiplier according to what the player wants
  if (efficiency != -1) return efficiency;
  if (!ability) return 0;
  GameObserver * g = GameObserver::GetInstance();
  ActionStack * s = g->mLayers->stackLayer();
  Player * p = g->currentlyActing();
  if (s->has(ability)) return 0;

  MTGAbility * a = AbilityFactory::getCoreAbility(ability);

  if (!a){
    DebugTrace("FATAL: Ability is NULL in AIAction::getEfficiency()");
    return 0;
  }

  if (!((AIPlayer *)p)->canHandleCost(ability)) return 0;
	switch (a->aType){
    case MTGAbility::DAMAGER:
      {
        AADamager * aad = (AADamager *) a;
        if (!target){
          Targetable * _t = aad->getTarget();
          if (_t == p->opponent()) efficiency = 90;
          else efficiency = 0;
          break;
        }
        if ( p == target->controller()){
          efficiency = 0;
        }else if (aad->damage->getValue() >= target->toughness){
          efficiency = 100;
        }else if (target->toughness){
          efficiency = (50 * aad->damage->getValue()) / target->toughness;
        }else{
          efficiency = 0;
        }
        break;
      }
    case MTGAbility::STANDARD_REGENERATE:
      {
        MTGCardInstance * _target = (MTGCardInstance *)(a->target);
        efficiency = 0;
				if (!_target->regenerateTokens && g->getCurrentGamePhase() == Constants::MTG_PHASE_COMBATBLOCKERS && (_target->defenser || _target->blockers.size())){
          efficiency = 95;
        }

        //TODO If the card is the target of a damage spell
        break;
      }
		case MTGAbility::STANDARD_PREVENT:
      {
        MTGCardInstance * _target = (MTGCardInstance *)(a->target);
        efficiency = 0;//starts out low to avoid spamming it when its not needed.
				bool NeedPreventing;
				NeedPreventing = false;
				if(g->getCurrentGamePhase() == Constants::MTG_PHASE_COMBATBLOCKERS)
				{
					if((target->defenser || target->blockers.size()) && target->preventable < target->getNextOpponent()->power) NeedPreventing = true;
				}
				if (p == target->controller() && NeedPreventing == true && !(target->getNextOpponent()->has(Constants::DEATHTOUCH) || target->getNextOpponent()->has(Constants::WITHER))){
					efficiency = 20 * (target->DangerRanking());//increase this chance to be used in combat if the creature blocking/blocked could kill the creature this chance is taking into consideration how good the creature is, best creature will always be the first "saved"..
					if(target->toughness == 1 && target->getNextOpponent()->power == 1) efficiency += 15;
					//small bonus added for the poor 1/1s, if we can save them, we will unless something else took precidence.
				}
				//note is the target is being blocked or blocking a creature with wither or deathtouch, it is not even considered for preventing as it is a waste.
        //if its combat blockers, it is being blocked or blocking, and has less prevents the the amount of damage it will be taking, the effeincy is increased slightly and totalled by the danger rank multiplier for final result.
        //TODO If the card is the target of a damage spell
        break;
      }
		case MTGAbility::STANDARD_EQUIP:
			{
			MTGCardInstance * _target = (MTGCardInstance *)(a->target);
			efficiency = 0;
			if (p == target->controller() && target->equipment <= 1 && !a->source->target)
			{
				efficiency = 20 * (target->DangerRanking());
				if(target->hasColor(5)) efficiency += 20;//this is to encourage Ai to equip white creatures in a weenie deck. ultimately it will depend on what had the higher dangerranking.
				if(target->power == 1 && target->toughness == 1 && target->isToken == 0) efficiency += 10; //small bonus to encourage equipping nontoken 1/1 creatures.
			}
			if (p == target->controller() && target->equipment > 2 && !a->source->target)
			{
				efficiency = 15 * (target->DangerRanking());
			}
      break;
			}

		case MTGAbility::STANDARD_LEVELUP:
      {
        MTGCardInstance * _target = (MTGCardInstance *)(a->target);
        efficiency = 0;
        Counter * targetCounter = NULL;
				int currentlevel = 0;
				if(_target)
				{
         if(_target->counters && _target->counters->hasCounter("level",0,0))
				 {
				 targetCounter = _target->counters->hasCounter("level",0,0);
			 	 currentlevel = targetCounter->nb;
				 }
				}
				if (currentlevel < _target->level){
          efficiency = 85;
					efficiency += currentlevel;//increase the efficeincy of leveling up by a small amount equal to current level.
				}
        break;
      }

    case MTGAbility::MANA_PRODUCER: //can't use mana producers right now :/
      efficiency = 0;
      break;
    default:
			if (target){
        AbilityFactory af;
        int suggestion = af.abilityEfficiency(a, p, MODE_ABILITY);
        if ((suggestion == BAKA_EFFECT_BAD && p==target->controller()) ||(suggestion == BAKA_EFFECT_GOOD && p!=target->controller())){
          efficiency =0;
        }else{
          efficiency = WRand() % 5; //Small percentage of chance for unknown abilities
        }
      }else{
        efficiency = WRand() % 10;
      }
      break;
  }
  if (p->game->hand->nb_cards == 0) efficiency = (int) ((float) efficiency * 1.3); //increase chance of using ability if hand is empty
  if (ability->cost){
    ExtraCosts * ec = ability->cost->extraCosts;
    if (ec) efficiency = efficiency / 3;  //Decrease chance of using ability if there is an extra cost to use the ability
  }
	return efficiency;
}




int AIPlayer::createAbilityTargets(MTGAbility * a, MTGCardInstance * c, map<AIAction *, int, CmpAbilities> * ranking){
  if (!a->tc){
    AIAction * as = NEW AIAction(a,c,NULL);
    (*ranking)[as] = 1;
    return 1;
  }
  GameObserver * g = GameObserver::GetInstance();
  for (int i = 0; i < 2; i++){
    Player * p = g->players[i];
    MTGGameZone * playerZones[] = {p->game->graveyard, p->game->library, p->game->hand, p->game->inPlay};
    for (int j = 0; j < 4; j++){
      MTGGameZone * zone = playerZones[j];
      for (int k=0; k < zone->nb_cards; k++){
        MTGCardInstance * t = zone->cards[k];
        if (a->tc->canTarget(t)){

          AIAction * as = NEW AIAction(a,c,t);
          (*ranking)[as] = 1;
        }
      }
    }
  }
  return 1;
}

int AIPlayer::selectAbility(){
  map<AIAction *, int,CmpAbilities>ranking;
  list<int>::iterator it;
  GameObserver * g = GameObserver::GetInstance();
  //This loop is extrmely inefficient. TODO: optimize!
  ManaCost * totalPotentialMana = getPotentialMana();
  for (int i = 1; i < g->mLayers->actionLayer()->mCount; i++){ //0 is not a mtgability...hackish
    MTGAbility * a = ((MTGAbility *)g->mLayers->actionLayer()->mObjects[i]);
    //Skip mana abilities for performance
    if (dynamic_cast<AManaProducer*>(a)) continue;
    //Make sure we can use the ability
    for (int j=0; j < game->inPlay->nb_cards; j++){
      MTGCardInstance * card =  game->inPlay->cards[j];       
      if (a->isReactingToClick(card,totalPotentialMana)){ //This test is to avod the huge call to getPotentialManaCost after that
        ManaCost * pMana = getPotentialMana(card);
        if (a->isReactingToClick(card,pMana))
          createAbilityTargets(a, card, &ranking);
        delete(pMana);
      }
    }
  }
  delete totalPotentialMana;

  if (ranking.size()){
    AIAction * a = ranking.begin()->first;
    int chance = 1;
    if (!forceBestAbilityUse) chance = 1 + WRand() % 100;
    if (getEfficiency(a) < chance){
      a = NULL;
    }else{
      DebugTrace("AIPlayer:Using Activated ability");
      tapLandsForMana(a->ability->cost,a->click);
      clickstream.push(a);
    }
    map<AIAction *, int, CmpAbilities>::iterator it2;
    for (it2 = ranking.begin(); it2!=ranking.end(); it2++){
      if (a != it2->first) delete(it2->first);
    }
  }
  return 1;
}



int AIPlayer::interruptIfICan(){
  GameObserver * g = GameObserver::GetInstance();

  if (g->mLayers->stackLayer()->askIfWishesToInterrupt == this){
      if (!clickstream.empty()) g->mLayers->stackLayer()->cancelInterruptOffer();
      else g->mLayers->stackLayer()->setIsInterrupting(this);
      return 1;
  }
  return 0;
}

int AIPlayer::effectBadOrGood(MTGCardInstance * card, int mode, TargetChooser * tc){
  int id = card->getMTGId();
  AbilityFactory af;
  int autoGuess = af.magicText(id,NULL,card, mode, tc);
  if (autoGuess) return autoGuess;
  return BAKA_EFFECT_DONTKNOW;
}



int AIPlayer::chooseTarget(TargetChooser * _tc, Player * forceTarget){
  vector<Targetable *>potentialTargets;
  TargetChooser * tc = _tc;
  int nbtargets = 0;
  GameObserver * gameObs = GameObserver::GetInstance();
  int checkOnly = 0;
  if (tc){
    checkOnly = 1;
  }else{
    tc = gameObs->getCurrentTargetChooser();
  }
  if (!tc) return 0;
  if (!(gameObs->currentlyActing() == this)) return 0;
  Player * target = forceTarget;
  
  if (!target){
    target = this;
    int cardEffect = effectBadOrGood(tc->source, MODE_TARGET, tc);
    if (cardEffect != BAKA_EFFECT_GOOD){
      target = this->opponent();
    }
  }

  if (!tc->alreadyHasTarget(target) &&  tc->canTarget(target) && nbtargets < 50){
    for (int i = 0; i < 3; i++){ //Increase probability to target a player when this is possible
      potentialTargets.push_back(target);
      nbtargets++;
    }
    if (checkOnly) return 1;
  }
  MTGPlayerCards * playerZones = target->game;
  MTGGameZone * zones[] = {playerZones->hand,playerZones->library,playerZones->inPlay, playerZones->graveyard};
  for (int j = 0; j < 4; j++){
    MTGGameZone * zone = zones[j];
    for (int k=0; k< zone->nb_cards; k++){
      MTGCardInstance * card = zone->cards[k];
      if (!tc->alreadyHasTarget(card) && tc->canTarget(card)  && nbtargets < 50){
	      if (checkOnly) return 1;
	      int multiplier = 1;
	      if (getStats() && getStats()->isInTop(card,10)){
	        multiplier++;
	        if (getStats()->isInTop(card,5)){
	          multiplier++;
	          if (getStats()->isInTop(card,3)){
	            multiplier++;
	          }
	        }
	      }
	      for (int l=0; l < multiplier; l++){
	        potentialTargets.push_back(card);
	        nbtargets++;
	      }
      }
    }
  }
  if (nbtargets){
    int i = WRand() % nbtargets;
    int type = potentialTargets[i]->typeAsTarget();
    switch(type){
    case TARGET_CARD:
      {
	MTGCardInstance * card = ((MTGCardInstance *) potentialTargets[i]);
	clickstream.push(NEW AIAction(card));
	return 1;
	break;
      }
    case TARGET_PLAYER:
      {
	Player * player = ((Player *) potentialTargets[i]);
	clickstream.push(NEW AIAction(player));
	return 1;
	break;
      }
    }
  }
  //Couldn't find any valid target,
  //usually that's because we played a card that has bad side effects (ex: when X comes into play, return target land you own to your hand)
  //so we try again to choose a target in the other player's field...
  if (checkOnly) return 0;
  int cancel = gameObs->cancelCurrentAction();
  if ( !cancel && !forceTarget) return chooseTarget(_tc,target->opponent());

  //ERROR!!!
  return 0;
}

int AIPlayer::getCreaturesInfo(Player * player, int neededInfo , int untapMode, int canAttack){
  int result = 0;
  CardDescriptor cd;
  cd.init();
  cd.setType("Creature");
  cd.unsecureSetTapped(untapMode);
  MTGCardInstance * card = NULL;
  while((card = cd.nextmatch(player->game->inPlay, card))){
    if (!canAttack || card->canAttack()){
      if (neededInfo == INFO_NBCREATURES){
	result++;
      }else{
	result+=card->power;
      }
    }
  }
  return result;
}



int AIPlayer::chooseAttackers(){
  //Attack with all creatures
  //How much damage can the other player do during his next Attack ?
  int opponentForce = getCreaturesInfo(opponent(),INFO_CREATURESPOWER);
  int opponentCreatures = getCreaturesInfo(opponent(), INFO_NBCREATURES);
  int myForce = getCreaturesInfo(this,INFO_CREATURESPOWER,-1,1);
  int myCreatures = getCreaturesInfo(this, INFO_NBCREATURES, -1,1);
  bool attack = ((myCreatures > opponentCreatures) || (myForce > opponentForce) || (myForce > 2*opponent()->life));
  if (agressivity > 80 && !attack && life > opponentForce) {
    opponentCreatures = getCreaturesInfo(opponent(), INFO_NBCREATURES,-1);
    opponentForce = getCreaturesInfo(opponent(),INFO_CREATURESPOWER,-1);
    attack = (myCreatures >= opponentCreatures && myForce > opponentForce) || (myForce > opponentForce) || (myForce > opponent()->life);
  }
  printf("Choose attackers : %i %i %i %i -> %i\n", opponentForce, opponentCreatures, myForce, myCreatures, attack);
  if (attack){
    CardDescriptor cd;
    cd.init();
    cd.setType("creature");
    MTGCardInstance * card = NULL;
    GameObserver * g = GameObserver::GetInstance();
    MTGAbility * a =  g->mLayers->actionLayer()->getAbility(MTGAbility::MTG_ATTACK_RULE);
    while((card = cd.nextmatch(game->inPlay, card))){
      g->mLayers->actionLayer()->reactToClick(a,card);
    }
  }
  return 1;
}

/* Can I first strike my oponent and get away with murder ? */
int AIPlayer::canFirstStrikeKill(MTGCardInstance * card, MTGCardInstance *ennemy){
  if (ennemy->has(Constants::FIRSTSTRIKE) || ennemy->has(Constants::DOUBLESTRIKE)) return 0;
  if (!(card->has(Constants::FIRSTSTRIKE) || card->has(Constants::DOUBLESTRIKE))) return 0;
  if (!(card->power >= ennemy->toughness)) return 0;
	if (!(card->power >= ennemy->toughness + 1) && ennemy->has(Constants::FLANKING)) return 0;
  return 1;
}

int AIPlayer::chooseBlockers(){
  map<MTGCardInstance *, int> opponentsToughness;
  int opponentForce = getCreaturesInfo(opponent(),INFO_CREATURESPOWER);
  //int opponentCreatures = getCreaturesInfo(opponent(), INFO_NBCREATURES, -1);
  //int myForce = getCreaturesInfo(this,INFO_CREATURESPOWER);
  //int myCreatures = getCreaturesInfo(this, INFO_NBCREATURES, -1);
  CardDescriptor cd;
  cd.init();
  cd.setType("Creature");
  cd.unsecureSetTapped(-1);
  MTGCardInstance * card = NULL;
  GameObserver * g = GameObserver::GetInstance();
  MTGAbility * a =  g->mLayers->actionLayer()->getAbility(MTGAbility::MTG_BLOCK_RULE);

  while((card = cd.nextmatch(game->inPlay, card))){
    g->mLayers->actionLayer()->reactToClick(a,card);
    int set = 0;
    while(!set){
      if (!card->defenser){
	      set = 1;
      }else{
	      MTGCardInstance * attacker = card->defenser;
	      map<MTGCardInstance *,int>::iterator it = opponentsToughness.find(attacker);
	      if ( it == opponentsToughness.end()){
	        opponentsToughness[attacker] = attacker->toughness;
	        it = opponentsToughness.find(attacker);
	      }
	      if (opponentsToughness[attacker] > 0 && getStats() && getStats()->isInTop(attacker,3,false)){
	        opponentsToughness[attacker]-= card->power;
	        set = 1;
	    }else{
      g->mLayers->actionLayer()->reactToClick(a,card);
	}
      }
    }
  }
  card = NULL;
  while((card = cd.nextmatch(game->inPlay, card))){
    if (card->defenser && opponentsToughness[card->defenser] > 0){
      while (card->defenser){

        g->mLayers->actionLayer()->reactToClick(a,card);
      }
    }
  }
  card = NULL;
  while((card = cd.nextmatch(game->inPlay, card))){
    if(!card->defenser){
      g->mLayers->actionLayer()->reactToClick(a,card);
      int set = 0;
      while(!set){
	if (!card->defenser){
	  set = 1;
	}else{
	  MTGCardInstance * attacker = card->defenser;
	  if (opponentsToughness[attacker] <= 0 ||
    (card->toughness <= attacker->power && opponentForce*2 <life  && !canFirstStrikeKill(card,attacker))  ||
		attacker->nbOpponents()>1){
      g->mLayers->actionLayer()->reactToClick(a,card);
	  }else{
	    set = 1;
	  }
	}
      }
    }
  }
  return 1;
}

int AIPlayer::orderBlockers(){

  GameObserver * g = GameObserver::GetInstance();
  if (ORDER == g->combatStep && g->currentPlayer==this)
    {
      DebugTrace("AIPLAYER: order blockers");
      g->userRequestNextGamePhase(); //TODO clever rank of blockers
      return 1;
    }

  return 0;
}

int AIPlayer::affectCombatDamages(CombatStep step){
  GameObserver * g = GameObserver::GetInstance();
  GuiCombat *  gc = g->mLayers->combatLayer();
  for (vector<AttackerDamaged*>::iterator attacker = gc->attackers.begin(); attacker != gc->attackers.end(); ++attacker)
          gc->autoaffectDamage(*attacker, step);
  return 1;
}

//TODO: Deprecate combatDamages
int AIPlayer::combatDamages(){
  //int result = 0;
  GameObserver * gameObs = GameObserver::GetInstance();
  int currentGamePhase = gameObs->getCurrentGamePhase();

  if (currentGamePhase == Constants::MTG_PHASE_COMBATBLOCKERS) return orderBlockers();

  if (currentGamePhase != Constants::MTG_PHASE_COMBATDAMAGE) return 0;

  return 0;

}


AIStats * AIPlayer::getStats(){
  if (!stats){
    char statFile[512];
    sprintf(statFile, RESPATH"/ai/baka/stats/%s.stats", opponent()->deckFileSmall.c_str());
    stats = NEW AIStats(this, statFile);
  }
  return stats;
}

AIPlayer * AIPlayerFactory::createAIPlayer(MTGAllCards * collection, Player * opponent, int deckid){
  char deckFile[512];
  char avatarFile[512];
  char deckFileSmall[512];

  if (deckid == GameStateDuel::MENUITEM_EVIL_TWIN){ //Evil twin
    sprintf(deckFile, "%s", opponent->deckFile.c_str());
    DebugTrace(opponent->deckFile);  
    sprintf(avatarFile, "%s", "baka.jpg");
    sprintf(deckFileSmall, "%s", "ai_baka_eviltwin");
  }else{
    if (!deckid){
      int nbdecks = 0;
      int found = 1;
      while (found){
        found = 0;
        char buffer[512];
        sprintf(buffer, RESPATH"/ai/baka/deck%i.txt",nbdecks+1);
        std::ifstream file(buffer);
        if(file){
          found = 1;
          file.close();
          nbdecks++;
        }
      }
      if (!nbdecks) return NULL;
      deckid = 1 + WRand() % (nbdecks);
    }
    sprintf(deckFile, RESPATH"/ai/baka/deck%i.txt",deckid);
    sprintf(avatarFile, "avatar%i.jpg",deckid);
    sprintf(deckFileSmall, "ai_baka_deck%i",deckid);
  }
  
  MTGDeck * tempDeck = NEW MTGDeck(deckFile, collection);
  //MTGPlayerCards * deck = NEW MTGPlayerCards(tempDeck);
  AIPlayerBaka * baka = NEW AIPlayerBaka(tempDeck,deckFile, deckFileSmall, avatarFile);
  baka->deckId = deckid; 

  delete tempDeck;
  return baka;
}


MTGCardInstance * AIPlayerBaka::FindCardToPlay(ManaCost * pMana, const char * type){
  int maxCost = -1;
  MTGCardInstance * nextCardToPlay = NULL;
  MTGCardInstance * card = NULL;
  CardDescriptor cd;
  cd.init();
  cd.setType(type);
  card = NULL;
  while((card = cd.nextmatch(game->hand, card))){
    if (card->hasType(Subtypes::TYPE_CREATURE) && this->castrestrictedcreature < 0 && this->castrestrictedspell < 0) continue;
	if (card->hasType(Subtypes::TYPE_ENCHANTMENT) && this->castrestrictedspell < 0) continue;
    if (card->hasType(Subtypes::TYPE_ARTIFACT) && this->castrestrictedspell < 0) continue;
	if (card->hasType(Subtypes::TYPE_SORCERY) && this->castrestrictedspell < 0) continue;
	if (card->hasType(Subtypes::TYPE_INSTANT) && this->castrestrictedspell < 0) continue;
    if (card->hasType(Subtypes::TYPE_LAND) && !this->canPutLandsIntoPlay) continue;
    if (card->hasType(Subtypes::TYPE_LEGENDARY) && game->inPlay->findByName(card->name)) continue;
    int currentCost = card->getManaCost()->getConvertedCost();
    int hasX = card->getManaCost()->hasX();
    if ((currentCost > maxCost || hasX) && pMana->canAfford(card->getManaCost())){
      TargetChooserFactory tcf;
      TargetChooser * tc = tcf.createTargetChooser(card);
      int shouldPlayPercentage = 10;
      if (tc){
	      int hasTarget = (chooseTarget(tc));
	      delete tc;
	      if (!hasTarget)continue;
        shouldPlayPercentage = 90;
      }else{
        int shouldPlay = effectBadOrGood(card);
        if (shouldPlay == BAKA_EFFECT_GOOD){
          shouldPlayPercentage = 90;
        }else if(BAKA_EFFECT_DONTKNOW == shouldPlay){
          shouldPlayPercentage = 80;
        }
      }
      //Reduce the chances of playing a spell with X cost if available mana is low
      if (hasX){
        int xDiff = pMana->getConvertedCost() - currentCost;
        if (xDiff < 0) xDiff = 0;
        shouldPlayPercentage = shouldPlayPercentage - static_cast<int>((shouldPlayPercentage * 1.9f) / (1 + xDiff));
      }

      if (WRand() % 100 > shouldPlayPercentage) continue;
      nextCardToPlay = card;
      maxCost = currentCost;
      if(hasX) maxCost = pMana->getConvertedCost();
    }
  }
  return nextCardToPlay;
}

AIPlayerBaka::AIPlayerBaka(MTGDeck * deck, string file, string fileSmall, string avatarFile) : AIPlayer(deck, file, fileSmall) {
  mAvatarTex = resources.RetrieveTexture(avatarFile,RETRIEVE_LOCK,TEXTURE_SUB_AVATAR);
  
  if(!mAvatarTex){
    avatarFile = "baka.jpg";
    mAvatarTex = resources.RetrieveTexture(avatarFile,RETRIEVE_LOCK,TEXTURE_SUB_AVATAR);
  }

  if(mAvatarTex)
    mAvatar = resources.RetrieveQuad(avatarFile, 0, 0, 35, 50,"bakaAvatar",RETRIEVE_NORMAL,TEXTURE_SUB_AVATAR);
  else 
    mAvatar = NULL;

  initTimer();
}


void AIPlayerBaka::initTimer(){
  timer = 0.1f;
}

int AIPlayerBaka::computeActions(){
  GameObserver * g = GameObserver::GetInstance();
  Player * p = g->currentPlayer;
  if (!(g->currentlyActing() == this)) return 0;
  if (g->mLayers->actionLayer()->menuObject){
    g->mLayers->actionLayer()->doReactTo(0);
    return 1;
  }
  if (chooseTarget()) return 1;
  int currentGamePhase = g->getCurrentGamePhase();
  if (g->isInterrupting == this){ // interrupting
    selectAbility();
    return 1;
  }else if (p == this && g->mLayers->stackLayer()->count(0,NOT_RESOLVED) == 0){ //standard actions
    CardDescriptor cd;
    MTGCardInstance * card = NULL;
    switch(currentGamePhase){
    case Constants::MTG_PHASE_FIRSTMAIN:
    case Constants::MTG_PHASE_SECONDMAIN:
    {

      bool potential = false;
      ManaCost * currentMana = manaPool;
      if (!currentMana->getConvertedCost()){
        currentMana = getPotentialMana();
        potential = true;
      }

      nextCardToPlay = FindCardToPlay(currentMana, "land");
      //look for the most expensive creature we can afford
	  if(castrestrictedspell == 0 && nospellinstant == 0){ 
		if(onlyonecast == 0  || castcount < 2){
		  if(onlyoneinstant == 0  || castcount < 2){
	  if(castrestrictedcreature == 0 && nocreatureinstant == 0){
	    if (!nextCardToPlay) nextCardToPlay = FindCardToPlay(currentMana, "creature");
		 }
      //Let's Try an enchantment maybe ?
	  if (!nextCardToPlay) nextCardToPlay = FindCardToPlay(currentMana, "enchantment");
	  if (!nextCardToPlay) nextCardToPlay = FindCardToPlay(currentMana, "artifact");
      if (!nextCardToPlay) nextCardToPlay = FindCardToPlay(currentMana, "sorcery");
	  if (!nextCardToPlay) nextCardToPlay = FindCardToPlay(currentMana, "instant");
	      
		}
	   }
	  }
      if (potential) delete(currentMana);
      if (nextCardToPlay){
        if (potential){
          tapLandsForMana(nextCardToPlay->getManaCost());  
        }
        AIAction * a = NEW AIAction(nextCardToPlay);
        clickstream.push(a);
        return 1;
      }else{
        selectAbility();
      }
      break;
    }
    case Constants::MTG_PHASE_COMBATATTACKERS:
      chooseAttackers();
      break;
    default:
      selectAbility();
      break;
    }
  }else{
    cout << "my turn" << endl;
    switch(currentGamePhase){
    case Constants::MTG_PHASE_COMBATBLOCKERS:
      chooseBlockers();
      break;
    default:
      break;
    }
    return 1;
  }
  return 1;
};

int AIPlayer::receiveEvent(WEvent * event){
 if (getStats()) return getStats()->receiveEvent(event);
 return 0;
}

void AIPlayer::Render(){
#ifdef RENDER_AI_STATS
  if (getStats()) getStats()->Render();
#endif
}

int AIPlayerBaka::Act(float dt){
  GameObserver * g = GameObserver::GetInstance();

  if (!(g->currentlyActing() == this)){
    return 0;
  }

  int currentGamePhase = g->getCurrentGamePhase();

  oldGamePhase = currentGamePhase;

  timer-= dt;
  if (timer>0){
    return 0;
  }
  initTimer();
  if (combatDamages()){
    return 0;
  }
  interruptIfICan();
  if (!(g->currentlyActing() == this)){
    DebugTrace("Cannot interrupt");
    return 0;
  }
  if (clickstream.empty()) computeActions();
  if (clickstream.empty()){
    if (g->isInterrupting == this){
      g->mLayers->stackLayer()->cancelInterruptOffer(); //endOfInterruption();
    }else{
      g->userRequestNextGamePhase();
    }
  } else {
    AIAction * action = clickstream.front();
    action->Act();
    SAFE_DELETE(action);
    clickstream.pop();
  }
  return 1;
};

