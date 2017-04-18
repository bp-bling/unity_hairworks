using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace UTJ
{
    [ExecuteInEditMode]
    public class HairVisibility : MonoBehaviour
    {
        UTJ.HairInstance hairInstance = null;

        // Use this for initialization
        void Start()
        {

        }

        // Update is called once per frame
        void Update()
        {

        }

        // called when Unity render this object
        void OnWillRenderObject()
        {
            RenderHairInstances();
        }

        void OnRenderObject() 
        {
            EndRenderHairInstances();
        }

        private void RenderHairInstances()
        {
            GetHairInstance();
            if (hairInstance)
                hairInstance.HairRendering();
        }

        private void EndRenderHairInstances()
        {
            GetHairInstance();
            if (hairInstance)
                hairInstance.FinishedRenderingHair();
        }

        private void GetHairInstance()
        {
            if (hairInstance == null)
            {
                GameObject parent = transform.parent.gameObject;
                if (parent)
                {
                    hairInstance = parent.GetComponent<UTJ.HairInstance>();
                }
            }
        }

    }
}
